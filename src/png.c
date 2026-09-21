/* png.c - minimal PNG decoder (inflate + unfilter) for loading app icons.
 * Supports 8-bit RGB/RGBA/gray/gray+alpha/palette, non-interlaced. ~250 lines, no zlib. */
#include "tm.h"
#include <stdlib.h>
#include <string.h>

/* ---- inflate (RFC 1951) --------------------------------------------------- */
typedef struct { const unsigned char *d; size_t n, pos; unsigned bitbuf; int bitcnt; } BitIn;

static unsigned getbit(BitIn *b)
{
    if (!b->bitcnt) { if (b->pos >= b->n) return 0; b->bitbuf = b->d[b->pos++]; b->bitcnt = 8; }
    unsigned v = b->bitbuf & 1; b->bitbuf >>= 1; b->bitcnt--; return v;
}
static unsigned getbits(BitIn *b, int n) { unsigned v = 0; for (int i = 0; i < n; i++) v |= getbit(b) << i; return v; }

typedef struct { unsigned short count[16], symbol[320]; } Huff;

static void huff_build(Huff *h, const unsigned char *lens, int n)
{
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lens[i]]++;
    h->count[0] = 0;
    unsigned short offs[16]; offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (lens[i]) h->symbol[offs[lens[i]]++] = (unsigned short)i;
}

static int huff_decode(BitIn *b, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)getbit(b);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}

typedef struct { unsigned char *d; size_t n, cap; } Buf;
static int buf_put(Buf *o, unsigned char c)
{
    if (o->n >= o->cap) { o->cap = o->cap ? o->cap * 2 : 65536; unsigned char *nd = realloc(o->d, o->cap); if (!nd) return -1; o->d = nd; }
    o->d[o->n++] = c; return 0;
}

static const unsigned short lbase[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned short lext[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dbase[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned short dext[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int inflate_block(BitIn *b, Buf *o, const Huff *lc, const Huff *dc)
{
    for (;;) {
        int sym = huff_decode(b, lc);
        if (sym < 0) return -1;
        if (sym < 256) { if (buf_put(o, (unsigned char)sym)) return -1; }
        else if (sym == 256) return 0;
        else {
            sym -= 257; if (sym >= 29) return -1;
            int len = lbase[sym] + (int)getbits(b, lext[sym]);
            int ds = huff_decode(b, dc); if (ds < 0 || ds >= 30) return -1;
            size_t dist = dbase[ds] + getbits(b, dext[ds]);
            if (dist > o->n) return -1;
            for (int i = 0; i < len; i++) if (buf_put(o, o->d[o->n - dist])) return -1;
        }
    }
}

static int inflate(const unsigned char *src, size_t n, Buf *o)
{
    BitIn b = { src, n, 0, 0, 0 };
    int final;
    do {
        final = (int)getbit(&b);
        int type = (int)getbits(&b, 2);
        if (type == 0) {
            b.bitcnt = 0;
            if (b.pos + 4 > n) return -1;
            unsigned len = src[b.pos] | (src[b.pos + 1] << 8); b.pos += 4;
            if (b.pos + len > n) return -1;
            for (unsigned i = 0; i < len; i++) if (buf_put(o, src[b.pos++])) return -1;
        } else if (type == 1) {
            static Huff lc, dc; static int init;
            if (!init) {
                unsigned char l[288]; int i;
                for (i = 0; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
                huff_build(&lc, l, 288);
                for (i = 0; i < 30; i++) l[i] = 5; huff_build(&dc, l, 30); init = 1;
            }
            if (inflate_block(&b, o, &lc, &dc)) return -1;
        } else if (type == 2) {
            int hlit = (int)getbits(&b, 5) + 257, hdist = (int)getbits(&b, 5) + 1, hclen = (int)getbits(&b, 4) + 4;
            static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            unsigned char lens[320]; memset(lens, 0, sizeof lens);
            for (int i = 0; i < hclen; i++) lens[order[i]] = (unsigned char)getbits(&b, 3);
            Huff clh; huff_build(&clh, lens, 19);
            int i = 0;
            while (i < hlit + hdist) {
                int sym = huff_decode(&b, &clh); if (sym < 0) return -1;
                if (sym < 16) lens[i++] = (unsigned char)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) { if (!i) return -1; val = lens[i - 1]; rep = 3 + (int)getbits(&b, 2); }
                    else if (sym == 17) rep = 3 + (int)getbits(&b, 3);
                    else rep = 11 + (int)getbits(&b, 7);
                    if (i + rep > hlit + hdist) return -1;
                    while (rep--) lens[i++] = (unsigned char)val;
                }
            }
            Huff lc, dc; huff_build(&lc, lens, hlit); huff_build(&dc, lens + hlit, hdist);
            if (inflate_block(&b, o, &lc, &dc)) return -1;
        } else return -1;
    } while (!final);
    return 0;
}

/* ---- PNG ------------------------------------------------------------------- */
static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }

int png_decode(const unsigned char *data, size_t len, uint32_t **out, int *ow, int *oh)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (len < 33 || memcmp(data, sig, 8)) return -1;
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    unsigned char pal[256 * 4]; int npal = 0; for (int i = 0; i < 256; i++) pal[i * 4 + 3] = 255;
    Buf z = { 0, 0, 0 };
    size_t pos = 8;
    while (pos + 8 <= len) {
        unsigned cl = be32(data + pos); const unsigned char *ct = data + pos + 4, *cd = data + pos + 8;
        if (pos + 12 + cl > len) break;
        if (!memcmp(ct, "IHDR", 4)) { w = (int)be32(cd); h = (int)be32(cd + 4); depth = cd[8]; ctype = cd[9]; interlace = cd[12]; }
        else if (!memcmp(ct, "PLTE", 4)) { npal = (int)cl / 3; for (int i = 0; i < npal && i < 256; i++) { pal[i * 4] = cd[i * 3]; pal[i * 4 + 1] = cd[i * 3 + 1]; pal[i * 4 + 2] = cd[i * 3 + 2]; } }
        else if (!memcmp(ct, "tRNS", 4)) { if (ctype == 3) for (unsigned i = 0; i < cl && i < 256; i++) pal[i * 4 + 3] = cd[i]; }
        else if (!memcmp(ct, "IDAT", 4)) { for (unsigned i = 0; i < cl; i++) buf_put(&z, cd[i]); }
        else if (!memcmp(ct, "IEND", 4)) break;
        pos += 12 + cl;
    }
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    int okdepth = (depth == 8 || depth == 16) || (ctype == 0 && (depth == 1 || depth == 2 || depth == 4)) || (ctype == 3 && (depth == 1 || depth == 2 || depth == 4));
    if (!ch || !okdepth || interlace || w <= 0 || h <= 0 || w > 1024 || h > 1024 || z.n < 2) { free(z.d); return -1; }
    Buf raw = { 0, 0, 0 };
    int rc = inflate(z.d + 2, z.n - 2, &raw);    /* skip zlib header */
    free(z.d);
    int bpp = (ch * depth + 7) / 8; if (bpp < 1) bpp = 1;      /* bytes per pixel for filtering */
    size_t stride = ((size_t)w * ch * depth + 7) / 8;
    if (rc || raw.n < (stride + 1) * h) { free(raw.d); return -1; }
    unsigned char *prev = calloc(1, stride), *cur = malloc(stride);
    uint32_t *px = malloc((size_t)w * h * 4);
    const unsigned char *rp = raw.d;
    for (int y = 0; y < h; y++) {
        int ft = *rp++;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0, x = rp[i];
            switch (ft) { case 1: x += a; break; case 2: x += b; break; case 3: x += (a + b) / 2; break; case 4: x += paeth(a, b, c); break; }
            cur[i] = (unsigned char)x;
        }
        rp += stride;
        for (int x = 0; x < w; x++) {
            unsigned v[4] = { 0, 0, 0, 255 }, r, g, bl, al = 255;
            for (int k = 0; k < ch; k++) {
                if (depth == 8) v[k] = cur[x * ch + k];
                else if (depth == 16) v[k] = cur[(x * ch + k) * 2];               /* take high byte */
                else { size_t bit = ((size_t)x * ch + k) * depth; unsigned byte = cur[bit / 8]; v[k] = (byte >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                       if (ctype == 0) v[k] = v[k] * 255 / ((1u << depth) - 1); }
            }
            switch (ctype) {
            case 0: r = g = bl = v[0]; break;
            case 2: r = v[0]; g = v[1]; bl = v[2]; break;
            case 3: r = pal[v[0] * 4]; g = pal[v[0] * 4 + 1]; bl = pal[v[0] * 4 + 2]; al = pal[v[0] * 4 + 3]; break;
            case 4: r = g = bl = v[0]; al = v[1]; break;
            default: r = v[0]; g = v[1]; bl = v[2]; al = v[3]; break;
            }
            px[y * w + x] = (al << 24) | (r << 16) | (g << 8) | bl;
        }
        unsigned char *t = prev; prev = cur; cur = t;
    }
    free(prev); free(cur); free(raw.d);
    *out = px; *ow = w; *oh = h;
    return 0;
}
