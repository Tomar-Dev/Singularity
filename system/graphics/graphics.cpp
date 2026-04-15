// system/graphics/graphics.cpp
#include "system/graphics/graphics.h"
#include "memory/kheap.h"
#include "memory/pmm.h"
#include "memory/paging.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "archs/cpu/x86_64/core/cpuid.h"
extern "C" {
    void memcpy_nt_avx(void* dest, const void* src, size_t count);
    void fill_rect_avx(uint32_t* buffer, uint32_t color, size_t pixel_count);
    void safe_memcpy_asm(void* dest, const void* src, size_t count);
    
    void* kmalloc_contiguous(size_t size);
    void kfree_contiguous(void* ptr, size_t size);
    uint64_t get_physical_address(uint64_t virt);
    void* ioremap(uint64_t phys, uint32_t size, uint64_t flags);
    void iounmap(void* virt, uint32_t size);
}

GraphicsBuffer::GraphicsBuffer(size_t sz) : size(sz), handle(0) {
    virt_addr = kmalloc_contiguous(size);
    if (virt_addr) {
        phys_addr = get_physical_address((uint64_t)virt_addr);
    } else {
        phys_addr = 0;
    }
}

GraphicsBuffer::~GraphicsBuffer() {
    if (virt_addr) {
        kfree_contiguous(virt_addr, size);
    }
}

void* GraphicsBuffer::map() {
    return virt_addr;
}

void GraphicsBuffer::bindToGPU(GraphicsDevice* dev) {
    if (!dev || !virt_addr) return;
}

Surface::Surface(uint32_t w, uint32_t h) : width(w), height(h), owns_buffer(true), gpu_buffer(nullptr) {
    pitch = width * 4;
    buffer = (uint32_t*)kmalloc_aligned(pitch * height, 64);
    if (buffer) memset(buffer, 0, pitch * height);
}

Surface::Surface(uint32_t w, uint32_t h, uint32_t* existing_buffer) 
    : width(w), height(h), buffer(existing_buffer), owns_buffer(false), gpu_buffer(nullptr) 
{
    pitch = width * 4;
}

Surface::~Surface() {
    if (owns_buffer && buffer) {
        kfree_aligned(buffer);
    }
    if (gpu_buffer) delete gpu_buffer;
}

void Surface::clear(uint32_t color) {
    if (!buffer) return;
    if (cpu_info.has_avx) {
        fill_rect_avx(buffer, color, width * height);
    } else {
        size_t count = width * height;
        for(size_t i=0; i<count; i++) buffer[i] = color;
    }
}

void Surface::putPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= width || (uint32_t)y >= height) return;
    buffer[y * width + x] = color;
}

void Surface::fillRect(int x, int y, uint32_t w, uint32_t h, uint32_t color) {
    if (!buffer) return;
    
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((uint32_t)x >= width || (uint32_t)y >= height) return;
    if (x + w > width || x + w < (uint32_t)x) w = width - x; // FIX 27: GFX Overflow
    if (y + h > height) h = height - y;
    
    if (w == 0 || h == 0) return;

    if (w == width) {
        if (cpu_info.has_avx) {
            fill_rect_avx(buffer + (y * width), color, w * h);
        } else {
            for (uint32_t i = 0; i < w * h; i++) buffer[y * width + i] = color;
        }
        return;
    }

    for (uint32_t row = 0; row < h; row++) {
        uint32_t* line_ptr = buffer + ((y + row) * width) + x;
        if (cpu_info.has_avx && w >= 8) {
            fill_rect_avx(line_ptr, color, w);
        } else {
            for (uint32_t col = 0; col < w; col++) line_ptr[col] = color;
        }
    }
}

void Surface::blit(Surface* src, int x, int y) {
    if (!buffer || !src) return;

    int src_x = 0;
    int src_y = 0;
    int w = src->getWidth();
    int h = src->getHeight();

    if (x < 0) { src_x = -x; w += x; x = 0; }
    if (y < 0) { src_y = -y; h += y; y = 0; }
    
    if ((uint32_t)x >= width || (uint32_t)y >= height) return;
    if (x + w > (int)width) w = width - x;
    if (y + h > (int)height) h = height - y;
    
    if (w <= 0 || h <= 0) return;

    uint32_t* dest_buf = this->buffer;
    uint32_t* src_buf = src->getBuffer();
    uint32_t dest_pitch_px = this->width;
    uint32_t src_pitch_px = src->getWidth();

    for (int row = 0; row < h; row++) {
        uint32_t* d = dest_buf + ((y + row) * dest_pitch_px) + x;
        uint32_t* s = src_buf + ((src_y + row) * src_pitch_px) + src_x;
        
        if (cpu_info.has_avx && w >= 16) {
            safe_memcpy_asm(d, s, w * 4);
        } else {
            memcpy(d, s, w * 4);
        }
    }
}

void Surface::uploadToGPU(GraphicsDevice* dev) {
    if (!dev) return;
    
    if (!gpu_buffer) {
        gpu_buffer = new GraphicsBuffer(width * height * 4);
    }
    
    void* gpu_mem = gpu_buffer->map();
    if (gpu_mem) {
        memcpy(gpu_mem, buffer, width * height * 4);
        
    }
}

static GraphicsDevice* primaryDisplay = nullptr;
static Surface* mainBackbuffer = nullptr;

namespace GraphicsManager {
    void registerDisplay(GraphicsDevice* dev) {
        if (!dev) return;
        
        if (primaryDisplay == nullptr) {
            primaryDisplay = dev;
            printf("[GFX] Primary Display set to: %s\n", dev->getName());
        }
    }

    GraphicsDevice* getPrimaryDisplay() {
        return primaryDisplay;
    }
    
    Surface* getBackbuffer() {
        return mainBackbuffer;
    }
}
