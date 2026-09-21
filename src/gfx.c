/* gfx.c - tiny software rasteriser (no dependencies, ARGB32 buffer) */
#include "tm.h"
#include <string.h>
#include <stdlib.h>

void gfx_init(Gfx *g, uint32_t *px, int w, int h, int scale)
{
    g->px = px; g->w = w; g->h = h; g->s = scale < 1 ? 1 : scale;
    gfx_noclip(g);
}

void gfx_noclip(Gfx *g) { g->cx0 = 0; g->cy0 = 0; g->cx1 = g->w; g->cy1 = g->h; }

void gfx_clip(Gfx *g, int x, int y, int w, int h)
{
    g->cx0 = x < 0 ? 0 : x;
    g->cy0 = y < 0 ? 0 : y;
    g->cx1 = x + w > g->w ? g->w : x + w;
    g->cy1 = y + h > g->h ? g->h : y + h;
}

static inline void put(Gfx *g, int x, int y, uint32_t c)
{
    if (x >= g->cx0 && x < g->cx1 && y >= g->cy0 && y < g->cy1)
        g->px[y * g->w + x] = c;
}

void gfx_fill(Gfx *g, int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < g->cx0 ? g->cx0 : x, y0 = y < g->cy0 ? g->cy0 : y;
    int x1 = x + w > g->cx1 ? g->cx1 : x + w, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = g->px + j * g->w;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}

uint32_t gfx_lerp(uint32_t a, uint32_t b, float t)
{
    if (t < 0) t = 0; if (t > 1) t = 1;
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void gfx_blend(Gfx *g, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = x < g->cx0 ? g->cx0 : x, y0 = y < g->cy0 ? g->cy0 : y;
    int x1 = x + w > g->cx1 ? g->cx1 : x + w, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255, ia = 255 - a;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = g->px + j * g->w;
        for (int i = x0; i < x1; i++) {
            uint32_t d = row[i];
            int r = (((d >> 16) & 255) * ia + cr * a) >> 8;
            int gg = (((d >> 8) & 255) * ia + cg * a) >> 8;
            int b = ((d & 255) * ia + cb * a) >> 8;
            row[i] = 0xff000000u | (r << 16) | (gg << 8) | b;
        }
    }
}

void gfx_hline(Gfx *g, int x, int y, int w, uint32_t c) { gfx_fill(g, x, y, w, g->s, c); }
void gfx_vline(Gfx *g, int x, int y, int h, uint32_t c) { gfx_fill(g, x, y, g->s, h, c); }

void gfx_rect(Gfx *g, int x, int y, int w, int h, uint32_t c)
{
    gfx_hline(g, x, y, w, c);
    gfx_hline(g, x, y + h - g->s, w, c);
    gfx_vline(g, x, y, h, c);
    gfx_vline(g, x + w - g->s, y, h, c);
}

void gfx_line(Gfx *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (g->s == 1) put(g, x0, y0, c); else gfx_fill(g, x0, y0, g->s, g->s, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void glyph(Gfx *g, int x, int y, int ch, uint32_t c, int s)
{
    if (ch < 32 || ch > 126) ch = '?';
    const char *rows = tm_font[ch - 32];
    for (int r = 0; r < FONT_H; r++)
        for (int col = 0; col < FONT_W; col++)
            if (rows[r * FONT_W + col] == '#') {
                if (s == 1) put(g, x + col, y + r, c);
                else gfx_fill(g, x + col * s, y + r * s, s, s, c);
            }
}

int gfx_text_s(Gfx *g, int x, int y, const char *s, uint32_t c, int scale)
{
    int x0 = x;
    for (; *s; s++) { glyph(g, x, y, (unsigned char)*s, c, scale); x += FONT_ADV * scale; }
    return x - x0;
}

int gfx_text(Gfx *g, int x, int y, const char *s, uint32_t c) { return gfx_text_s(g, x, y, s, c, g->s); }
int gfx_textw_s(Gfx *g, const char *s, int scale) { (void)g; return (int)strlen(s) * FONT_ADV * scale; }
int gfx_textw(Gfx *g, const char *s) { return gfx_textw_s(g, s, g->s); }

void gfx_text_r(Gfx *g, int xr, int y, const char *s, uint32_t c)
{
    gfx_text(g, xr - gfx_textw(g, s), y, s, c);
}

/* draw text clipped to maxw, adding "..." when truncated */
void gfx_text_clip(Gfx *g, int x, int y, int maxw, const char *s, uint32_t c)
{
    int adv = FONT_ADV * g->s;
    int n = (int)strlen(s), fit = maxw / adv;
    if (fit <= 0) return;
    if (n <= fit) { gfx_text(g, x, y, s, c); return; }
    char buf[256];
    if (fit > 255) fit = 255;
    int keep = fit > 3 ? fit - 3 : fit;
    memcpy(buf, s, keep);
    if (fit > 3) memcpy(buf + keep, "...", 3);
    buf[fit] = 0;
    gfx_text(g, x, y, buf, c);
}

/* small filled triangle (sort arrows / tree expanders) */
void gfx_tri(Gfx *g, int x, int y, int size, int up, uint32_t c)
{
    for (int r = 0; r < size; r++) {
        int half = up ? r : size - 1 - r;
        gfx_fill(g, x + (size - 1 - half) * g->s, y + r * g->s, (2 * half + 1) * g->s, g->s, c);
    }
}
