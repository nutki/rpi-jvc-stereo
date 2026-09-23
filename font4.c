#include "font4.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int draw_color = 16;
static int rotation_center_x = 256 * 1.6;
static int rotation_center_y = 48 * -2.2;

void font4_set_color(int c) {
    draw_color = c;
}

void font4_set_rotation_center(int cx, int cy) {
    rotation_center_x = cx;
    rotation_center_y = cy;
}

static inline unsigned char to4(unsigned char v) {
    return (v * draw_color) >> 8;
}

static inline void set_px4(unsigned char *buf, int pitch,
                           int buf_w, int buf_h,
                           int x, int y, unsigned char v4)
{
    if (!v4) return;
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
    render_text_angle(f, buf, buf_w, buf_h, font_size, x, y, max_w,
                      pitch, 0.0, text);
}

int cx = 0, cy = 200;
void render_text_angle(font4_t *f,
                       unsigned char *buf,
                       int buf_w, int buf_h,
                       int font_size,
                       int x, int y,
                       int max_w,
                       int pitch,
                       double angle_degrees,
                       const char *text)
{
    FT_Set_Pixel_Sizes(f->face, 0, font_size);

    double angle = angle_degrees * (3.14159265358979323846 / 180.0);
    double angle_cos = cos(angle);
    double angle_sin = sin(angle);
    FT_Matrix transform = {
        (FT_Fixed)lround(angle_cos * 65536.0),
        (FT_Fixed)lround(angle_sin * 65536.0),
        (FT_Fixed)lround(-angle_sin * 65536.0),
        (FT_Fixed)lround(angle_cos * 65536.0)
    };
    FT_Set_Transform(f->face, &transform, NULL);

    double dx = (double)x - rotation_center_x;
    double dy = (double)y - rotation_center_y;
    double x_rot = rotation_center_x + dx * angle_cos - dy * angle_sin;
    double y_rot = rotation_center_y + dx * angle_sin + dy * angle_cos;

    FT_Vector pen = {
        (FT_Pos)lround(x_rot) << 6,
        (FT_Pos)lround(y_rot) << 6
    };

    for (const unsigned char *p = (const unsigned char*)text; *p; p++) {
        if (*p == '\n') {
            pen.x = (FT_Pos)x << 6;
            pen.x -= (FT_Pos)lround(font_size * angle_sin * 64.0);
            pen.y += (FT_Pos)lround(font_size * angle_cos * 64.0);
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
        int base_x = (int)(pen.x >> 6);
        int base_y = (int)(pen.y >> 6);
        FT_Vector delta = {
            pen.x - ((FT_Pos)base_x << 6),
            -(pen.y - ((FT_Pos)base_y << 6))
        };
        FT_Set_Transform(f->face, &transform, &delta);

        FT_Int32 load_flags = FT_LOAD_RENDER;
        if (angle_degrees != 0.0)
            load_flags |= FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;

        if (FT_Load_Char(f->face, utf8_decoded, load_flags))
            continue;

        FT_GlyphSlot g = f->face->glyph;
        FT_Bitmap *bm = &g->bitmap;

        int gx = base_x + g->bitmap_left;
        int gy = base_y - g->bitmap_top;

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

        pen.x += g->advance.x;
        pen.y -= g->advance.y;
        if ((pen.x >> 6) >= x + max_w)
            break;
    }

    FT_Set_Transform(f->face, NULL, NULL);
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
    render_textf_angle(f, buf, buf_w, buf_h, font_size, x, y, max_w,
                       pitch, 0.0, fmt);
}

void render_textf_angle(font4_t *f,
                        unsigned char *buf,
                        int buf_w, int buf_h,
                        int font_size,
                        int x, int y,
                        int max_w,
                        int pitch,
                        double angle_degrees,
                        const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    render_text_angle(f, buf, buf_w, buf_h, font_size, x, y, max_w, pitch,
                      angle_degrees, tmp);
}
