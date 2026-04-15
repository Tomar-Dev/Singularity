// drivers/video/framebuffer.cpp
#include "drivers/video/framebuffer.h"
#include "drivers/video/font.h"
#include "memory/paging.h"
#include "memory/kheap.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "drivers/serial/serial.h"
#include "system/console/console.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/core/multiboot.h"
#include "archs/cpu/x86_64/memory/mtrr.h"
#include "system/process/process.h" 

extern "C" {
    void memcpy_nt_avx(void* dest, const void* src, size_t count);
    void fill_rect_avx(uint32_t* buffer, uint32_t color, size_t pixel_count);
    void memcpy64_asm(void* dest, const void* src, size_t count);
    void memcpy_sse2_wc(void* dest, const void* src, size_t count); 
    void draw_char_row_avx(void* dest, uint32_t fg, uint32_t bg, uint8_t font_bits);
}

#define TILE_SIZE 16
#define GLYPH_W 8
#define GLYPH_H 16

static GOPDisplay* g_gop = nullptr;
framebuffer_info_t fb_info = {0, 0, 0, 0, 0};
uint32_t* backbuffer = nullptr;
static void (*g_gpu_flush_cb)(void) = nullptr;
static bool is_virtualbox_gpu = false;

extern volatile bool global_panic_active; 
static volatile int gop_is_flushing = 0;

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    hal_io_outl(0xCF8, address);
    return hal_io_inl(0xCFC);
}

static uint64_t find_pci_framebuffer_address() {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read_config(bus, slot, 0, 0x00);
            if ((vendor_device & 0xFFFF) == 0xFFFF) continue;
            uint16_t vendor_id = vendor_device & 0xFFFF;
            uint32_t class_rev = pci_read_config(bus, slot, 0, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            
            if (class_code == 0x03) { 
                if (vendor_id == 0x80EE) is_virtualbox_gpu = true;
                
                uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
                uint32_t bar1 = pci_read_config(bus, slot, 0, 0x14);
                uint32_t bar2 = pci_read_config(bus, slot, 0, 0x18);
                uint64_t addr = 0;
                
                if ((bar0 & 1) == 0) { 
                    if (((bar0 >> 1) & 0x3) == 2) addr = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0);
                    else addr = (bar0 & 0xFFFFFFF0);
                }
                if (addr == 0 || addr < 0x100000) {
                     if ((bar2 & 1) == 0) addr = (bar2 & 0xFFFFFFF0);
                }
                if (addr > 0x100000) return addr;
            }
        }
    }
    return 0;
}

GOPDisplay::GOPDisplay(void* tag_ptr) : GraphicsDevice("fb0") {
    struct multiboot_tag_framebuffer* tag = (struct multiboot_tag_framebuffer*)tag_ptr;
    currentMode.width = tag->common.framebuffer_width;
    currentMode.height = tag->common.framebuffer_height;
    currentMode.pitch = tag->common.framebuffer_pitch;
    currentMode.bpp = tag->common.framebuffer_bpp;
    currentMode.framebuffer_phys = tag->common.framebuffer_addr;
    
    fb_info.width = currentMode.width;
    fb_info.height = currentMode.height;
    fb_info.pitch = currentMode.pitch;
    fb_info.bpp = currentMode.bpp;
    fb_info.address = currentMode.framebuffer_phys;
    
    buffer_ptr = nullptr;
    dirty_tiles = nullptr;
    flush_buffer = nullptr;
    
    find_pci_framebuffer_address();
    use_avx = cpu_info.has_avx && !is_virtualbox_gpu; 
    
    spinlock_init(&lock);
}

GOPDisplay::~GOPDisplay() {
    if (buffer_ptr) kfree(buffer_ptr);
    if (dirty_tiles) kfree(dirty_tiles);
    if (flush_buffer) kfree(flush_buffer);
}

int GOPDisplay::ioctl(uint32_t request, void* arg) {
    if (request == FBIOGET_VSCREENINFO) { 
        if (!arg) return -1;
        struct fb_var_screeninfo* info = (struct fb_var_screeninfo*)arg;
        info->xres = fb_info.width;
        info->yres = fb_info.height;
        info->xres_virtual = fb_info.width;
        info->yres_virtual = fb_info.height;
        info->bits_per_pixel = fb_info.bpp;
        info->xoffset = 0;
        info->yoffset = 0;
        info->grayscale = 0;
        return 0;
    }
    if (request == FBIO_WAITFORVSYNC) { 
        task_sleep(4); 
        return 0;
    }
    return -1;
}

int GOPDisplay::readOffset(uint64_t offset, uint32_t size, uint8_t* buffer) {
    if (!backbuffer) return 0;
    uint32_t max_size = fb_info.width * fb_info.height * (fb_info.bpp / 8);
    if (offset >= max_size) return 0;
    if (offset + size > max_size) size = max_size - (uint32_t)offset;
    memcpy(buffer, (uint8_t*)backbuffer + offset, size);
    return size;
}

int GOPDisplay::writeOffset(uint64_t offset, uint32_t size, const uint8_t* buffer) {
    if (!backbuffer) return 0;
    uint32_t max_size = fb_info.width * fb_info.height * (fb_info.bpp / 8);
    if (offset >= max_size) return 0;
    if (offset + size > max_size) size = max_size - (uint32_t)offset;
    
    memcpy((uint8_t*)backbuffer + offset, buffer, size);
    
    uint32_t start_y = offset / fb_info.pitch;
    uint32_t end_y = (offset + size) / fb_info.pitch;
    framebuffer_mark_dirty_rect(0, start_y, fb_info.width, (end_y - start_y) + 1);
    
    return size;
}

__attribute__((no_stack_protector))
int GOPDisplay::init() {
    if (currentMode.framebuffer_phys < 0x1000000) {
        uint64_t pci_addr = find_pci_framebuffer_address();
        if (pci_addr > 0x1000000) {
            currentMode.framebuffer_phys = pci_addr;
            fb_info.address = pci_addr;
        } else return 0;
    }

    uint64_t size = (uint64_t)currentMode.pitch * currentMode.height;
    mtrr_set_wc(currentMode.framebuffer_phys, size);
    currentMode.framebuffer_virt = ioremap(currentMode.framebuffer_phys, size, PAGE_PRESENT | PAGE_WRITE | PAGE_WRITE_COMBINE);
    
    if (!currentMode.framebuffer_virt) return 0;
    
    initBackbuffer();
    
    if (backbuffer) {
        size_t count = fb_info.width * fb_info.height;
        if (use_avx) fill_rect_avx(backbuffer, 0xFF000000, count);
        else for(size_t i=0; i<count; i++) backbuffer[i] = 0xFF000000;
        
        if (use_avx) memcpy_nt_avx(currentMode.framebuffer_virt, backbuffer, size);
        else if (is_virtualbox_gpu) memcpy_sse2_wc(currentMode.framebuffer_virt, backbuffer, size);
        else memcpy64_asm(currentMode.framebuffer_virt, backbuffer, size);
    }

    console_init();
    GraphicsManager::registerDisplay(this);
    DeviceManager::registerDevice(this);
    
    g_gop = this;
    return 1;
}

__attribute__((no_stack_protector))
void GOPDisplay::initBackbuffer() {
    size_t size = currentMode.pitch * currentMode.height;
    if (buffer_ptr) kfree(buffer_ptr);
    if (dirty_tiles) kfree(dirty_tiles);
    if (flush_buffer) kfree(flush_buffer);

    buffer_ptr = (uint32_t*)kmalloc(size);
    if (!buffer_ptr) buffer_ptr = (uint32_t*)currentMode.framebuffer_virt;
    else {
        if (use_avx) fill_rect_avx(buffer_ptr, 0, fb_info.width * fb_info.height);
        else memset(buffer_ptr, 0, size);
    }
    
    backbuffer = buffer_ptr;
    tiles_x = (currentMode.width + TILE_SIZE - 1) / TILE_SIZE;
    tiles_y = (currentMode.height + TILE_SIZE - 1) / TILE_SIZE;
    
    size_t tile_map_size = tiles_x * tiles_y;
    dirty_tiles = (uint8_t*)kmalloc(tile_map_size);
    flush_buffer = (uint8_t*)kmalloc(tile_map_size); 
    if (dirty_tiles) memset(dirty_tiles, 1, tile_map_size); 
}

bool GOPDisplay::setMode(uint32_t width, uint32_t height) { return (width == currentMode.width && height == currentMode.height); }

void GOPDisplay::markDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!dirty_tiles) return;
    if (x >= currentMode.width || y >= currentMode.height) return;
    
    uint32_t max_x = x + w;
    uint32_t max_y = y + h;
    if (max_x > currentMode.width) max_x = currentMode.width;
    if (max_y > currentMode.height) max_y = currentMode.height;
    
    int start_tx = x / TILE_SIZE;
    int start_ty = y / TILE_SIZE;
    int end_tx = (max_x + TILE_SIZE - 1) / TILE_SIZE;
    int end_ty = (max_y + TILE_SIZE - 1) / TILE_SIZE;
    
    uint64_t flags = 0;
    if (!global_panic_active) flags = spinlock_acquire(&lock);
    
    for (int ty = start_ty; ty < end_ty; ty++) {
        int row_offset = ty * tiles_x;
        for (int tx = start_tx; tx < end_tx; tx++) {
            dirty_tiles[row_offset + tx] = 1;
        }
    }
    
    if (!global_panic_active) spinlock_release(&lock, flags);
}

void GOPDisplay::flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (g_gpu_flush_cb) { g_gpu_flush_cb(); return; }

    // FIX: Concurrent Flush Zırhı! (İki çekirdek aynı anda VRAM kopyalayamaz, ekran donmaz)
    if (!global_panic_active) {
        if (__atomic_test_and_set(&gop_is_flushing, __ATOMIC_ACQUIRE)) {
            return; 
        }
    }

    if (!buffer_ptr || !currentMode.framebuffer_virt || !dirty_tiles || !flush_buffer) {
        if (!global_panic_active) __atomic_clear(&gop_is_flushing, __ATOMIC_RELEASE);
        return;
    }

    bool force_full = (x == 0 && y == 0 && w == currentMode.width && h == currentMode.height);
    
    uint64_t flags = 0;
    if (!global_panic_active) flags = spinlock_acquire(&lock);

    size_t map_size = tiles_x * tiles_y;
    int dirty_count = 0;
    if (!force_full) {
        for(size_t i=0; i<map_size; i++) {
            if (dirty_tiles[i]) {
                flush_buffer[i] = 1;
                dirty_count++;
            } else {
                flush_buffer[i] = 0;
            }
        }
    }
    memset(dirty_tiles, 0, map_size);

    if (!global_panic_active) spinlock_release(&lock, flags);

    uint8_t* vram = (uint8_t*)currentMode.framebuffer_virt;
    uint8_t* ram = (uint8_t*)buffer_ptr;
    size_t total_size = currentMode.pitch * currentMode.height;

    __asm__ volatile("mfence" ::: "memory");

    if (force_full || dirty_count > (int)(map_size / 4)) {
        if (use_avx) memcpy_nt_avx(vram, ram, total_size);
        else if (is_virtualbox_gpu) memcpy_sse2_wc(vram, ram, total_size);
        else memcpy64_asm(vram, ram, total_size);
        
        if (!global_panic_active) __atomic_clear(&gop_is_flushing, __ATOMIC_RELEASE);
        return;
    }

    if (dirty_count == 0) {
        if (!global_panic_active) __atomic_clear(&gop_is_flushing, __ATOMIC_RELEASE);
        return;
    }

    uint32_t pitch = currentMode.pitch;
    
    for (int ty = 0; ty < tiles_y; ty++) {
        int row_offset = ty * tiles_x;
        int tx = 0;
        while (tx < tiles_x) {
            if (flush_buffer[row_offset + tx]) {
                int run = 1;
                while ((tx + run < tiles_x) && flush_buffer[row_offset + (tx + run)]) run++;
                
                uint32_t rect_x = tx * TILE_SIZE;
                uint32_t rect_y = ty * TILE_SIZE;
                uint32_t rect_w = run * TILE_SIZE;
                uint32_t rect_h = TILE_SIZE;
                
                if (rect_x + rect_w > currentMode.width) rect_w = currentMode.width - rect_x;
                if (rect_y + rect_h > currentMode.height) rect_h = currentMode.height - rect_y;
                
                size_t row_bytes = rect_w * 4;
                for (uint32_t row = 0; row < rect_h; row++) {
                    uint32_t offset = ((rect_y + row) * pitch) + (rect_x * 4);
                    if (use_avx && row_bytes >= 64) memcpy_nt_avx(vram + offset, ram + offset, row_bytes);
                    else if (is_virtualbox_gpu && row_bytes >= 16) memcpy_sse2_wc(vram + offset, ram + offset, row_bytes);
                    else memcpy64_asm(vram + offset, ram + offset, row_bytes);
                }
                tx += run;
            } else {
                tx++;
            }
        }
    }
    
    if (!global_panic_active) __atomic_clear(&gop_is_flushing, __ATOMIC_RELEASE);
}

static void draw_char_internal(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (!backbuffer) return;
    
    uint8_t uc = (uint8_t)c;
    const uint8_t* glyph = &font8x16[uc * 16];
    
    uint8_t* base_addr = (uint8_t*)backbuffer + (y * fb_info.pitch) + (x * 4);
    uint32_t pitch = fb_info.pitch;

    if (cpu_info.has_avx2) {
        for (int i = 0; i < 16; i++) draw_char_row_avx((void*)(base_addr + (i * pitch)), fg, bg, glyph[i]);
    } else {
        for (int i = 0; i < 16; i++) {
            uint32_t* row = (uint32_t*)(base_addr + (i * pitch));
            uint8_t bits = glyph[i];
            for (int j = 0; j < 8; j++) row[j] = (bits & (0x80 >> j)) ? fg : bg;
        }
    }
}

extern "C" {
    __attribute__((no_stack_protector))
    void init_gop_driver(void* multiboot_addr) {
        struct multiboot_tag* tag;
        uint8_t* ptr = (uint8_t*)multiboot_addr + 8;
        for (tag = (struct multiboot_tag*)ptr; tag->type != 0; tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
            if (tag->type == 8) { 
                GOPDisplay* gop = new GOPDisplay(tag);
                gop->init();
                return;
            }
        }
    }
    
    void framebuffer_set_driver(uint64_t virt_addr, uint32_t width, uint32_t height, uint32_t pitch, void (*flush_cb)(void)) {
        fb_info.width = width; fb_info.height = height; fb_info.pitch = pitch; fb_info.address = virt_addr;
        g_gpu_flush_cb = flush_cb;
        __asm__ volatile("mfence" ::: "memory");
        backbuffer = (uint32_t*)virt_addr;
        console_init();
    }
    
    void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
        if (backbuffer && x < fb_info.width && y < fb_info.height) {
            backbuffer[y * (fb_info.pitch / 4) + x] = color;
            if (g_gop && !g_gpu_flush_cb) g_gop->markDirty(x, y, 1, 1);
        }
    }

    uint32_t get_pixel(uint32_t x, uint32_t y) {
        if (backbuffer && x < fb_info.width && y < fb_info.height) return backbuffer[y * (fb_info.pitch / 4) + x];
        return 0;
    }

    void fill_rect(void* dest, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        uint32_t* buf = (uint32_t*)dest;
        if (!buf) buf = backbuffer;
        if (!buf) return;

        if (x >= fb_info.width || y >= fb_info.height) return;
        if (x + w > fb_info.width) w = fb_info.width - x;
        if (y + h > fb_info.height) h = fb_info.height - y;
        if (w == 0 || h == 0) return;

        if (cpu_info.has_avx) {
            for (uint32_t row = 0; row < h; row++) {
                uint32_t* line_ptr = buf + ((y + row) * (fb_info.pitch / 4)) + x;
                fill_rect_avx(line_ptr, color, w);
            }
        } else {
            for (uint32_t row = 0; row < h; row++) {
                uint32_t* line_ptr = buf + ((y + row) * (fb_info.pitch / 4)) + x;
                for (uint32_t col = 0; col < w; col++) line_ptr[col] = color;
            }
        }
        if (g_gop && !g_gpu_flush_cb) g_gop->markDirty(x, y, w, h);
    }
    
    void gop_put_pixel(uint32_t x, uint32_t y, uint32_t color) { put_pixel(x, y, color); }
    
    void gop_draw_string(int x, int y, const char* str, uint32_t color) {
        if (!backbuffer) return;
        int cx = x, cy = y;
        uint32_t bg = 0; 
        while (*str) {
            char c = *str++;
            if (c == '\n') { cx = x; cy += 16; continue; }
            draw_char_internal(c, cx, cy, color, bg);
            cx += 8;
        }
        int width = (cx - x);
        if (width > 0) framebuffer_mark_dirty_rect(x, y, width, 16);
    }
    
    void gop_flush_screen() {
        if (g_gpu_flush_cb) g_gpu_flush_cb(); 
        else if (g_gop) g_gop->flushAll(); 
    }
    
    void framebuffer_flush() {
        if (g_gpu_flush_cb) g_gpu_flush_cb();
        else if (g_gop) g_gop->flush(0, 0, 0, 0); 
    }
    
    void clear_screen(uint32_t color) {
        if (!backbuffer) return;
        if (cpu_info.has_avx) fill_rect_avx(backbuffer, color, fb_info.width * fb_info.height);
        else {
            size_t count = fb_info.width * fb_info.height;
            for(size_t i=0; i<count; i++) backbuffer[i] = color;
        }
        if (g_gop && !g_gpu_flush_cb) g_gop->markDirty(0, 0, fb_info.width, fb_info.height);
    }
    
    void framebuffer_mark_dirty_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        if (g_gop && !g_gpu_flush_cb) g_gop->markDirty(x, y, w, h);
    }
    
    void init_framebuffer(void* multiboot_addr) { init_gop_driver(multiboot_addr); }

    void framebuffer_force_unlock() {
        __atomic_clear(&gop_is_flushing, __ATOMIC_RELEASE);
        if (g_gop) g_gop->forceUnlock();
    }
}