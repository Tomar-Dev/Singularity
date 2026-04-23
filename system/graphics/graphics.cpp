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
    } else {
        // Null
    }
}

void* GraphicsBuffer::map() {
    return virt_addr;
}

void GraphicsBuffer::bindToGPU(GraphicsDevice* dev) {
    if (!dev || !virt_addr) {
        return;
    } else {
        // Bind logic
    }
}

Surface::Surface(uint32_t w, uint32_t h) : width(w), height(h), owns_buffer(true), gpu_buffer(nullptr) {
    pitch = width * 4;
    buffer = (uint32_t*)kmalloc_aligned(pitch * height, 64);
    if (buffer) {
        memset(buffer, 0, pitch * height);
    } else {
        // OOM
    }
}

Surface::Surface(uint32_t w, uint32_t h, uint32_t* existing_buffer) 
    : width(w), height(h), buffer(existing_buffer), owns_buffer(false), gpu_buffer(nullptr) 
{
    pitch = width * 4;
}

Surface::~Surface() {
    if (owns_buffer && buffer) {
        kfree_aligned(buffer);
    } else {
        // Not owned
    }
    if (gpu_buffer) {
        delete gpu_buffer;
    } else {
        // Null
    }
}

void Surface::clear(uint32_t color) {
    if (!buffer) {
        return;
    } else {
        // Valid
    }
    if (cpu_info.has_avx) {
        fill_rect_avx(buffer, color, width * height);
    } else {
        size_t count = width * height;
        for(size_t i=0; i<count; i++) buffer[i] = color;
    }
}

void Surface::putPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= width || (uint32_t)y >= height) {
        return;
    } else {
        // Valid
    }
    buffer[y * width + x] = color;
}

void Surface::fillRect(int x, int y, uint32_t w, uint32_t h, uint32_t color) {
    if (!buffer) {
        return;
    } else {
        // Valid
    }
    
    if (x < 0) { w += x; x = 0; } else { /* Valid */ }
    if (y < 0) { h += y; y = 0; } else { /* Valid */ }
    if ((uint32_t)x >= width || (uint32_t)y >= height) {
        return;
    } else {
        // Valid
    }
    if (x + w > width || x + w < (uint32_t)x) {
        w = width - x; 
    } else {
        // Valid
    }
    if (y + h > height) {
        h = height - y;
    } else {
        // Valid
    }
    
    if (w == 0 || h == 0) {
        return;
    } else {
        // Valid
    }

    if (w == width) {
        if (cpu_info.has_avx) {
            fill_rect_avx(buffer + (y * width), color, w * h);
        } else {
            for (uint32_t i = 0; i < w * h; i++) buffer[y * width + i] = color;
        }
        return;
    } else {
        // Partial
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
    if (!buffer || !src) {
        return;
    } else {
        // Valid
    }

    int src_x = 0;
    int src_y = 0;
    int w = src->getWidth();
    int h = src->getHeight();

    if (x < 0) { src_x = -x; w += x; x = 0; } else { /* Valid */ }
    if (y < 0) { src_y = -y; h += y; y = 0; } else { /* Valid */ }
    
    if ((uint32_t)x >= width || (uint32_t)y >= height) {
        return;
    } else {
        // Valid
    }
    if (x + w > (int)width) {
        w = width - x;
    } else {
        // Valid
    }
    if (y + h > (int)height) {
        h = height - y;
    } else {
        // Valid
    }
    
    if (w <= 0 || h <= 0) {
        return;
    } else {
        // Valid
    }

    uint32_t* dest_buf = this->buffer;
    uint32_t* src_buf = src->getBuffer();
    uint32_t dest_pitch_px = this->width;
    uint32_t src_pitch_px = src->getWidth();

    for (int row = 0; row < h; row++) {
        uint32_t* d = dest_buf + ((y + row) * dest_pitch_px) + x;
        uint32_t* s = src_buf + ((src_y + row) * src_pitch_px) + src_x;
        
        // PERFORMANS YAMASI: NT Store (Non-Temporal) kullanılarak VRAM kopyalaması hızlandırıldı.
        if (cpu_info.has_avx && w >= 16) {
            memcpy_nt_avx(d, s, w * 4);
        } else {
            memcpy(d, s, w * 4);
        }
    }
}

void Surface::uploadToGPU(GraphicsDevice* dev) {
    if (!dev) {
        return;
    } else {
        // Valid
    }
    
    if (!gpu_buffer) {
        gpu_buffer = new GraphicsBuffer(width * height * 4);
    } else {
        // Exists
    }
    
    void* gpu_mem = gpu_buffer->map();
    if (gpu_mem) {
        memcpy(gpu_mem, buffer, width * height * 4);
    } else {
        // Unmapped
    }
}

static GraphicsDevice* primaryDisplay = nullptr;
static Surface* mainBackbuffer = nullptr;

namespace GraphicsManager {
    void registerDisplay(GraphicsDevice* dev) {
        if (!dev) {
            return;
        } else {
            // Valid
        }
        
        if (primaryDisplay == nullptr) {
            primaryDisplay = dev;
            printf("[GFX] Primary Display set to: %s\n", dev->getName());
        } else {
            // Already set
        }
    }

    GraphicsDevice* getPrimaryDisplay() {
        return primaryDisplay;
    }
    
    Surface* getBackbuffer() {
        return mainBackbuffer;
    }
}