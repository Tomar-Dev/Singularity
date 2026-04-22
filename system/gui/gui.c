// system/gui/gui.c
#include "system/gui/gui.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "kernel/debug.h"
#include "archs/cpu/cpu_hal.h"
#include "kernel/config.h"

void draw_circle(uint32_t xc, uint32_t yc, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                put_pixel(xc + x, yc + y, color);
            }
        }
    }
}

void gui_mode_start() {
    clear_screen(0xFF000000); 
    
    uint32_t cx = fb_info.width / 2;
    uint32_t cy = fb_info.height / 2;
    
    draw_circle(cx, cy, 100, 0xFFFF0000); 
    
    char title_str[128];
    snprintf(title_str, sizeof(title_str), "%s GUI Environment", SINGULARITY_SYS_NAME);
    
    gop_draw_string(20, 20, title_str, 0xFFFFFFFF);
    gop_draw_string(20, 40, "Direct GOP Rendering (No Console)", 0xFFCCCCCC);
    
    char res_str[64];
    snprintf(res_str, sizeof(res_str), "Resolution: %dx%d @ %d bpp", 
             fb_info.width, fb_info.height, fb_info.bpp);
    
    int text_x = cx - (strlen(res_str) * 8) / 2; 
    int text_y = cy + 120;
    
    gop_draw_string(text_x, text_y, res_str, 0xFFFFFFFF);
    
    gop_draw_string(20, fb_info.height - 30, "Press any key to exit...", 0xFFAAAAAA);
    
    gop_flush_screen();

    while(1) {
        if (keyboard_has_input()) {
            keyboard_getchar();
            break;
        }
        hal_cpu_halt();
    }
    
    console_clear();
    console_set_auto_flush(true);
}