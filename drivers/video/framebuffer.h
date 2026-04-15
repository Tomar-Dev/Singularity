// drivers/video/framebuffer.h
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "archs/cpu/x86_64/sync/spinlock.h"

#ifdef __cplusplus
#include "system/graphics/graphics.h" 
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t address; 
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
} framebuffer_info_t;

extern framebuffer_info_t fb_info;
extern uint32_t* backbuffer; 

void init_framebuffer(void* multiboot_addr);
void init_gop_driver(void* multiboot_addr);
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t get_pixel(uint32_t x, uint32_t y);
void fill_rect(void* dest, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void clear_screen(uint32_t color);
void framebuffer_scroll_up(uint32_t pixels, uint32_t fill_color);
void framebuffer_flush(); 
void framebuffer_smart_flush(); 
void framebuffer_mark_dirty(uint32_t y, uint32_t height);
void framebuffer_mark_dirty_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void framebuffer_set_driver(uint64_t virt_addr, uint32_t width, uint32_t height, uint32_t pitch, void (*flush_cb)(void));
void gop_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void gop_draw_string(int x, int y, const char* str, uint32_t color); 
void gop_flush_screen();
void framebuffer_force_unlock(); 

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class GOPDisplay : public GraphicsDevice {
private:
    uint32_t* buffer_ptr;
    uint8_t* dirty_tiles;
    uint8_t* flush_buffer; 
    int tiles_x;
    int tiles_y;
    bool use_avx;
    
    spinlock_t lock; 
    
    void initBackbuffer();

public:
    GOPDisplay(void* tag_ptr);
    ~GOPDisplay() override;
    
    int init() override;
    
    bool setMode(uint32_t width, uint32_t height) override;
    void flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    void markDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void flushAll() override { flush(0, 0, currentMode.width, currentMode.height); }
    void forceUnlock() { spinlock_init(&lock); }

    int ioctl(uint32_t request, void* arg) override;
    int readOffset(uint64_t offset, uint32_t size, uint8_t* buffer) override;
    int writeOffset(uint64_t offset, uint32_t size, const uint8_t* buffer) override;
};

#endif

#endif // FIX: Eksik olan son endif eklendi.