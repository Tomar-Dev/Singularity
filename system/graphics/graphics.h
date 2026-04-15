// system/graphics/graphics.h
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stddef.h>
#include "system/device/device.h"
#define FBIOGET_VSCREENINFO 0x4600
#define FBIO_WAITFORVSYNC   0x4620

struct fb_var_screeninfo {
    uint32_t xres;           
    uint32_t yres;           
    uint32_t xres_virtual;   
    uint32_t yres_virtual;   
    uint32_t xoffset;        
    uint32_t yoffset;        
    uint32_t bits_per_pixel; 
    uint32_t grayscale;      
};

#define GFX_FORMAT_ARGB32 0

typedef struct {
    int x, y;
    uint32_t w, h;
} Rect;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint64_t framebuffer_phys; 
    void*    framebuffer_virt; 
} GraphicsMode;

#ifdef __cplusplus

class GraphicsBuffer {
private:
    uint64_t phys_addr;
    void* virt_addr;
    size_t size;
    uint32_t handle; 

public:
    GraphicsBuffer(size_t size);
    ~GraphicsBuffer();
    
    void* map();
    void unmap();
    uint64_t getPhys() const { return phys_addr; }
    uint32_t getHandle() const { return handle; }
    size_t getSize() const { return size; }
    void bindToGPU(class GraphicsDevice* dev);
};

class Surface {
private:
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t* buffer;
    bool owns_buffer; 
    GraphicsBuffer* gpu_buffer;

public:
    Surface(uint32_t w, uint32_t h);
    Surface(uint32_t w, uint32_t h, uint32_t* existing_buffer); 
    ~Surface();

    uint32_t* getBuffer() const { return buffer; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    uint32_t getPitch() const { return pitch; }

    void clear(uint32_t color);
    void fillRect(int x, int y, uint32_t w, uint32_t h, uint32_t color);
    void blit(Surface* src, int x, int y); 
    void putPixel(int x, int y, uint32_t color);
    void uploadToGPU(class GraphicsDevice* dev);
};

class GraphicsDevice : public Device {
protected:
    GraphicsMode currentMode;

public:
    GraphicsDevice(const char* name) : Device(name, DEV_DISPLAY) {}
    virtual ~GraphicsDevice() override {}

    virtual bool setMode(uint32_t width, uint32_t height) = 0;
    virtual GraphicsMode getMode() const { return currentMode; }
    virtual void flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    virtual void flushAll() { flush(0, 0, currentMode.width, currentMode.height); }

    virtual void hw_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        (void)x; (void)y; (void)w; (void)h; (void)color;
    }
    
    virtual uint32_t createResource(uint32_t w, uint32_t h, uint32_t format) { return 0; }
    virtual void attachBacking(uint32_t resource_id, uint64_t phys_addr, size_t size) {}
    virtual void transferToHost(uint32_t resource_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t offset) {}
};

namespace GraphicsManager {
    void registerDisplay(GraphicsDevice* dev);
    GraphicsDevice* getPrimaryDisplay();
    Surface* getBackbuffer(); 
}

#endif 
#endif
