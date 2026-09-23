#ifndef FONT4_H
#define FONT4_H

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FT_Library ft;
    FT_Face face;
} font4_t;

int font4_init(font4_t *f, const char *ttf_path);
void font4_destroy(font4_t *f);
void font4_set_color(int c);
void font4_set_rotation_center(int cx, int cy);

void render_text(font4_t *f,
                 unsigned char *buf,
                 int buf_w, int buf_h,
                 int font_size,
                 int x, int y,
                 int max_w,
                 int pitch,
                 const char *text);

/* Positive angles rotate clockwise in the framebuffer's top-left-origin coordinates. */
void render_text_angle(font4_t *f,
                       unsigned char *buf,
                       int buf_w, int buf_h,
                       int font_size,
                       int x, int y,
                       int max_w,
                       int pitch,
                       double angle_degrees,
                       const char *text);

void render_textf(font4_t *f,
                  unsigned char *buf,
                  int buf_w, int buf_h,
                  int font_size,
                  int x, int y,
                  int max_w,
                  int pitch,
                  const char *fmt, ...);

void render_textf_angle(font4_t *f,
                        unsigned char *buf,
                        int buf_w, int buf_h,
                        int font_size,
                        int x, int y,
                        int max_w,
                        int pitch,
                        double angle_degrees,
                        const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
