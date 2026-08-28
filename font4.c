#include "font4.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int draw_color = 16;
void font4_set_color(int c) {
    draw_color = c;
}

static inline unsigned char to4(unsigned char v) {
    return (v * draw_color) >> 8;
}

static inline void set_px4(unsigned char *buf, int pitch,
                           int buf_w, int buf_h,
                           int x, int y, unsigned char v4)
{
    if ((unsigned)x >= (unsigned)buf_w ||
        (unsigned)y >= (unsigned)buf_h)
        return;

    int byte_index = y * pitch + (x >> 1);
    unsigned char old = buf[byte_index];

    if ((x & 1) == 0)
        old = (old & 0x0F) | ((v4 & 0x0F) << 4);
    else
        old = (old & 0xF0) | (v4 & 0x0F);

    buf[byte_index] = old;
}

int font4_init(font4_t *f, const char *ttf_path)
{
    if (f->ft) return 0;  // Already initialized
    if (FT_Init_FreeType(&f->ft))
        return 0;

    if (FT_New_Face(f->ft, ttf_path, 0, &f->face))
        return 0;

    return 1;
}

void font4_destroy(font4_t *f)
{
    FT_Done_Face(f->face);
    FT_Done_FreeType(f->ft);
}

void render_text(font4_t *f,
                 unsigned char *buf,
                 int buf_w, int buf_h,
                 int font_size,
                 int x, int y,
                 int max_w,
                 int pitch,
                 const char *text)
{
    FT_Set_Pixel_Sizes(f->face, 0, font_size);

    int pen_x = x;
    int pen_y = y;

    for (const unsigned char *p = (const unsigned char*)text; *p; p++) {
        if (*p == '\n') {
            pen_x = x;
            pen_y += font_size;
            continue;
        }

        int utf8_decoded = 0;
        if (*p < 0x80) {
            utf8_decoded = *p;
        } else if ((*p & 0xE0) == 0xC0) {
            utf8_decoded = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
            p += 1;
        } else if ((*p & 0xF0) == 0xE0) {
            utf8_decoded = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 2;
        } else if ((*p & 0xF8) == 0xF0) {
            utf8_decoded = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            p += 3;
        } else {
            continue;  // Invalid UTF-8, skip
        }
        if (FT_Load_Char(f->face, utf8_decoded, FT_LOAD_RENDER))
            continue;

        FT_GlyphSlot g = f->face->glyph;
        FT_Bitmap *bm = &g->bitmap;

        int gx = pen_x + g->bitmap_left;
        int gy = pen_y - g->bitmap_top;

        for (int j = 0; j < bm->rows; j++) {
            for (int i = 0; i < bm->width; i++) {
                int px = gx + i;
                if (px >= x + max_w)
                    break;

                int py = gy + j;
                unsigned char v = bm->buffer[j * bm->pitch + i];
                set_px4(buf, pitch, buf_w, buf_h, px, py, to4(v));
            }
        }

        pen_x += g->advance.x >> 6;
        if (pen_x >= x + max_w)
            break;
    }
}

void render_textf(font4_t *f,
                  unsigned char *buf,
                  int buf_w, int buf_h,
                  int font_size,
                  int x, int y,
                  int max_w,
                  int pitch,
                  const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    render_text(f, buf, buf_w, buf_h, font_size, x, y, max_w, pitch, tmp);
}
