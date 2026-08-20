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

void render_text(font4_t *f,
                 unsigned char *buf,
                 int buf_w, int buf_h,
                 int font_size,
                 int x, int y,
                 int max_w,
                 int pitch,
                 const char *text);

void render_textf(font4_t *f,
                  unsigned char *buf,
                  int buf_w, int buf_h,
                  int font_size,
                  int x, int y,
                  int max_w,
                  int pitch,
                  const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
