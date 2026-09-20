#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "font4.h"

// FrameBuffer structure and functions
typedef struct {
    uint8_t* buffer;
    int width;
    int height;
    int buffer_size;
} FrameBuffer;

void framebuffer_init(void);
FrameBuffer* framebuffer_create_with_buffer(int width, int height, uint8_t* buffer);
FrameBuffer* framebuffer_create(int width, int height);
FrameBuffer* framebuffer_create_from_png(const char* filename);
void framebuffer_destroy(FrameBuffer* fb);
void framebuffer_set_pixel(FrameBuffer* fb, int x, int y, uint8_t color);
uint8_t framebuffer_get_pixel(FrameBuffer* fb, int x, int y);
void framebuffer_fill(FrameBuffer* fb, uint8_t color);
void framebuffer_rect(FrameBuffer* fb, int x, int y, int w, int h, uint8_t color);
void framebuffer_fill_rect(FrameBuffer* fb, int x, int y, int w, int h, uint8_t color);
void framebuffer_blit(FrameBuffer* dest, FrameBuffer* src, int dest_x, int dest_y);
void framebuffer_draw_text(FrameBuffer* fb, int size, int x, int y, const char* text);
void framebuffer_draw_text_angle(FrameBuffer* fb, int size, int x, int y,
                                 double angle_degrees, const char* text);
void framebuffer_draw_text_fmt(FrameBuffer* fb, int size, int x, int y, const char* fmt, ...);
void framebuffer_draw_icon(FrameBuffer* fb, int size, int x, int y, const char* icon);
#define FA_WIFI "\uf1eb"
#define FA_VOLUME_UP "\uf028"
#define FA_VOLUME_DOWN "\uf027"
#define FA_VOLUME_OFF "\uf026"
#define FA_VOLUME_MUTE "\uf6a9"
#define FA_FAN "\uf863"
#define FA_TEMPERATURE_HIGH "\uf769"
#define FA_MUSIC "\uf001"
#define FA_PLUG "\uf1e6"
#define FA_FILM "\uf008"
#define FA_DOWNLOAD "\uf019"
#define FA_TV "\uf26c"
#define FA_RADIO "\uf8d7"
#define FA_HEAPHONES "\uf025"

// SH1122 OLED display structure and functions
typedef struct {
    int spi_fd;
    int width;
    int height;
    FrameBuffer* fb;
    font4_t font;
} SH1122;

SH1122* sh1122_create(const char* spi_device, int width, int height, int offset);
void sh1122_destroy(SH1122* oled);
void sh1122_show(SH1122* oled);
void sh1122_poweroff(SH1122* oled);
void sh1122_poweron(SH1122* oled);
void sh1122_contrast(SH1122* oled, uint8_t contrast);
void sh1122_flip(SH1122* oled);
void sh1122_invert(SH1122* oled, int invert);

#ifdef __cplusplus
}
#endif

#endif
