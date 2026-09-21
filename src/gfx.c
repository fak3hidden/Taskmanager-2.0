/* gfx.c - tiny software rasteriser (no dependencies, ARGB32 buffer)
 * Anti-aliased proportional text from baked DejaVu Sans, rounded rects, alpha blits. */
#include "tm.h"
#include "fontdata.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

void gfx_init(Gfx *g, uint32_t *px, int w, int h, int scale)
{
    g->px = px; g->w = w; g->h = h; g->s = scale < 1 ? 1 : scale; g->face = F_UI;
    gfx_noclip(g);
}

void gfx_noclip(Gfx *g) { g->cx0 = 0; g->cy0 = 0; g->cx1 = g->w; g->cy1 = g->h; }

void gfx_clip(Gfx *g, int x, int y, int w, int h)
{
    g->cx0 = x < 0 ? 0 : x;
    g->cy0 = y < 0 ? 0 : y;
    g->cx1 = x + w > g->w ? g->w : x + w;
    g->cy1 = y + h > g->h ? g->h : y + h;
    if (g->cx1 < g->cx0) g->cx1 = g->cx0;
    if (g->cy1 < g->cy0) g->cy1 = g->cy0;
}

static inline void put(Gfx *g, int x, int y, uint32_t c)
{
    if (x >= g->cx0 && x < g->cx1 && y >= g->cy0 && y < g->cy1)
        g->px[y * g->w + x] = c;
}

static inline uint32_t mix(uint32_t d, uint32_t c, int a)   /* a: 0..255 */
{
    int ia = 255 - a;
    int r = (((d >> 16) & 255) * ia + ((c >> 16) & 255) * a + 127) / 255;
    int gg = (((d >> 8) & 255) * ia + ((c >> 8) & 255) * a + 127) / 255;
    int b = ((d & 255) * ia + (c & 255) * a + 127) / 255;
    return 0xff000000u | (r << 16) | (gg << 8) | b;
}

static inline void blendpx(Gfx *g, int x, int y, uint32_t c, int a)
{
    if (a <= 0 || x < g->cx0 || x >= g->cx1 || y < g->cy0 || y >= g->cy1) return;
    uint32_t *p = &g->px[y * g->w + x];
    *p = a >= 255 ? (0xff000000u | c) : mix(*p, c, a);
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
    int r = ar + (int)((br - ar) * t), gg = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)bl;
}

void gfx_blend(Gfx *g, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = x < g->cx0 ? g->cx0 : x, y0 = y < g->cy0 ? g->cy0 : y;
    int x1 = x + w > g->cx1 ? g->cx1 : x + w, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = g->px + j * g->w;
        for (int i = x0; i < x1; i++) row[i] = mix(row[i], c, a);
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

/* rounded rectangle with anti-aliased corners; a = alpha 0..255 */
void gfx_rrect_a(Gfx *g, int x, int y, int w, int h, int r, uint32_t c, int a)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
    if (r <= 0) { if (a >= 255) gfx_fill(g, x, y, w, h, c); else gfx_blend(g, x, y, w, h, c, a); return; }
    /* middle band + top/bottom straight parts */
    if (a >= 255) { gfx_fill(g, x, y + r, w, h - 2 * r, c); gfx_fill(g, x + r, y, w - 2 * r, r, c); gfx_fill(g, x + r, y + h - r, w - 2 * r, r, c); }
    else { gfx_blend(g, x, y + r, w, h - 2 * r, c, a); gfx_blend(g, x + r, y, w - 2 * r, r, c, a); gfx_blend(g, x + r, y + h - r, w - 2 * r, r, c, a); }
    /* corners */
    float rf = (float)r;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            float dx = rf - (i + 0.5f), dy = rf - (j + 0.5f);
            float d = sqrtf(dx * dx + dy * dy);
            float cov = rf - d + 0.5f; if (cov <= 0) continue; if (cov > 1) cov = 1;
            int aa = (int)(cov * a);
            blendpx(g, x + i, y + j, c, aa);
            blendpx(g, x + w - 1 - i, y + j, c, aa);
            blendpx(g, x + i, y + h - 1 - j, c, aa);
            blendpx(g, x + w - 1 - i, y + h - 1 - j, c, aa);
        }
    }
}

void gfx_rrect(Gfx *g, int x, int y, int w, int h, int r, uint32_t c) { gfx_rrect_a(g, x, y, w, h, r, c, 255); }

void gfx_rrect_b(Gfx *g, int x, int y, int w, int h, int r, uint32_t fill, uint32_t border)
{
    gfx_rrect(g, x, y, w, h, r, border);
    int i = g->s;
    gfx_rrect(g, x + i, y + i, w - 2 * i, h - 2 * i, r - i > 0 ? r - i : 0, fill);
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

/* anti-aliased line (Xiaolin Wu), thickness = scale */
void gfx_line_aa(Gfx *g, float x0, float y0, float x1, float y1, uint32_t c)
{
    int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    if (steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    float dx = x1 - x0, dy = y1 - y0, grad = dx == 0 ? 1 : dy / dx;
    int xs = (int)floorf(x0 + 0.5f), xe = (int)floorf(x1 + 0.5f);
    float y = y0 + grad * (xs - x0);
    for (int x = xs; x <= xe; x++) {
        int iy = (int)floorf(y); float f = y - iy;
        for (int t = 0; t < g->s; t++) {
            int a1 = (int)((1 - f) * 255), a2 = (int)(f * 255);
            if (steep) { blendpx(g, iy + t, x, c, a1); blendpx(g, iy + 1 + t, x, c, a2); }
            else { blendpx(g, x, iy + t, c, a1); blendpx(g, x, iy + 1 + t, c, a2); }
        }
        y += grad;
    }
}

/* ---- text ---------------------------------------------------------------- */
static const Font *face(const Gfx *g)
{
    static const int map1[F_COUNT] = { 0, 1, 2, 6 }, map2[F_COUNT] = { 3, 4, 5, 7 };
    int f = g->face < 0 || g->face >= F_COUNT ? F_UI : g->face;
    return &fonts[g->s >= 2 ? map2[f] : map1[f]];
}

void gfx_font(Gfx *g, int f) { g->face = f; }
int gfx_fonth(Gfx *g) { return face(g)->line_h; }
int gfx_fontpx(Gfx *g) { return face(g)->px; }

int gfx_textw(Gfx *g, const char *s)
{
    const Font *f = face(g); int adv4 = 0;
    for (; *s; s++) { int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?'; adv4 += f->g[c - 32].adv4; }
    return (adv4 + 2) / 4;
}

int gfx_text(Gfx *g, int x, int y, const char *s, uint32_t c)
{
    const Font *f = face(g);
    int base = y + f->ascent, x4 = x * 4;
    for (; *s; s++) {
        int ch = (unsigned char)*s; if (ch < 32 || ch > 126) ch = '?';
        const Glyph *gl = &f->g[ch - 32];
        int gx = (x4 + 2) / 4 + gl->xo, gy = base + gl->yo;
        const uint8_t *d = f->data + gl->off;
        for (int j = 0; j < gl->h; j++)
            for (int i = 0; i < gl->w; i++) { int a = d[j * gl->w + i]; if (a) blendpx(g, gx + i, gy + j, c, a); }
        x4 += gl->adv4;
    }
    return (x4 + 2) / 4 - x;
}

void gfx_text_r(Gfx *g, int xr, int y, const char *s, uint32_t c) { gfx_text(g, xr - gfx_textw(g, s), y, s, c); }

/* vertically centred in a box of height h */
int gfx_text_v(Gfx *g, int x, int y, int h, const char *s, uint32_t c) { return gfx_text(g, x, y + (h - gfx_fonth(g)) / 2, s, c); }
void gfx_text_rv(Gfx *g, int xr, int y, int h, const char *s, uint32_t c) { gfx_text_r(g, xr, y + (h - gfx_fonth(g)) / 2, s, c); }
void gfx_text_mid(Gfx *g, int x, int y, int w, int h, const char *s, uint32_t c)
{
    gfx_text(g, x + (w - gfx_textw(g, s)) / 2, y + (h - gfx_fonth(g)) / 2, s, c);
}

/* draw text clipped to maxw, adding an ellipsis when truncated */
void gfx_text_clip(Gfx *g, int x, int y, int maxw, const char *s, uint32_t c)
{
    if (maxw <= 0) return;
    if (gfx_textw(g, s) <= maxw) { gfx_text(g, x, y, s, c); return; }
    char buf[256]; int n = (int)strlen(s); if (n > 250) n = 250;
    int dots = gfx_textw(g, "...");
    while (n > 0) {
        memcpy(buf, s, n); buf[n] = 0;
        if (gfx_textw(g, buf) + dots <= maxw) break;
        n--;
    }
    if (n <= 0) return;
    memcpy(buf + n, "...", 4);
    gfx_text(g, x, y, buf, c);
}

/* small filled triangle (sort chevrons) */
void gfx_tri(Gfx *g, int x, int y, int size, int up, uint32_t c)
{
    for (int r = 0; r < size; r++) {
        int half = up ? r : size - 1 - r;
        gfx_fill(g, x + (size - 1 - half) * g->s, y + r * g->s, (2 * half + 1) * g->s, g->s, c);
    }
}

/* straight-alpha ARGB image blit */
void gfx_blit(Gfx *g, int x, int y, const uint32_t *px, int w, int h)
{
    for (int j = 0; j < h; j++) {
        int dy = y + j; if (dy < g->cy0 || dy >= g->cy1) continue;
        for (int i = 0; i < w; i++) {
            uint32_t p = px[j * w + i]; int a = p >> 24;
            if (a) blendpx(g, x + i, dy, p & 0xffffff, a);
        }
    }
}

/* box-filter downscale/upscale of an ARGB image to size x size (straight alpha) */
void icon_scale(const uint32_t *src, int sw, int sh, uint32_t *dst, int size)
{
    for (int y = 0; y < size; y++) {
        int sy0 = y * sh / size, sy1 = (y + 1) * sh / size; if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < size; x++) {
            int sx0 = x * sw / size, sx1 = (x + 1) * sw / size; if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned long a = 0, r = 0, gg = 0, b = 0, n = 0;
            for (int j = sy0; j < sy1 && j < sh; j++)
                for (int i = sx0; i < sx1 && i < sw; i++) {
                    uint32_t p = src[j * sw + i]; unsigned pa = p >> 24;
                    a += pa; r += ((p >> 16) & 255) * pa; gg += ((p >> 8) & 255) * pa; b += (p & 255) * pa; n++;
                }
            if (!n || !a) { dst[y * size + x] = 0; continue; }
            dst[y * size + x] = ((a / n) << 24) | ((r / a) << 16) | ((gg / a) << 8) | (b / a);
        }
    }
}
