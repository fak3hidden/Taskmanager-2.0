#!/usr/bin/env python3
"""
bakefont.py - rasterise a TrueType font into a C header with anti-aliased
proportional bitmap glyphs.  Zero dependencies (own TTF parser + scanline
rasteriser) so the result can be regenerated anywhere.

    python3 tools/bakefont.py DejaVuSans.ttf DejaVuSans-Bold.ttf > src/fontdata.h

The output defines `const Font fonts[]` with these faces (index = FontId in tm.h):
    0 F_UI     regular 12px      3 F_UI2    regular 24px   (HiDPI)
    1 F_BOLD   bold    12px      4 F_BOLD2  bold    24px
    2 F_BIG    regular 20px      5 F_BIG2   regular 40px
    6 F_MID    regular 15px      7 F_MID2   regular 30px
"""
import math
import struct
import sys

FIRST, LAST = 32, 126


class TTF:
    def __init__(self, data):
        self.d = data
        n = struct.unpack_from(">H", data, 4)[0]
        self.tables = {}
        for i in range(n):
            tag, _, off, ln = struct.unpack_from(">4sIII", data, 12 + 16 * i)
            self.tables[tag.decode("latin1")] = (off, ln)
        head = self.tables["head"][0]
        self.upem = struct.unpack_from(">H", data, head + 18)[0]
        self.loc_long = struct.unpack_from(">h", data, head + 50)[0] == 1
        hhea = self.tables["hhea"][0]
        self.ascender, self.descender = struct.unpack_from(">hh", data, hhea + 4)
        self.nhm = struct.unpack_from(">H", data, hhea + 34)[0]
        self.nglyphs = struct.unpack_from(">H", data, self.tables["maxp"][0] + 4)[0]
        self._cmap()

    def _cmap(self):
        off = self.tables["cmap"][0]
        n = struct.unpack_from(">H", self.d, off + 2)[0]
        best = None
        for i in range(n):
            pid, eid, so = struct.unpack_from(">HHI", self.d, off + 4 + 8 * i)
            fmt = struct.unpack_from(">H", self.d, off + so)[0]
            if fmt == 4 and (best is None or (pid, eid) == (3, 1)):
                best = off + so
        if best is None:
            raise SystemExit("no format-4 cmap")
        segx2 = struct.unpack_from(">H", self.d, best + 6)[0]
        seg = segx2 // 2
        ends = struct.unpack_from(">%dH" % seg, self.d, best + 14)
        starts = struct.unpack_from(">%dH" % seg, self.d, best + 16 + segx2)
        deltas = struct.unpack_from(">%dh" % seg, self.d, best + 16 + 2 * segx2)
        ro_off = best + 16 + 3 * segx2
        ros = struct.unpack_from(">%dH" % seg, self.d, ro_off)
        self.cmap = {}
        for c in range(FIRST, LAST + 1):
            for i in range(seg):
                if starts[i] <= c <= ends[i]:
                    if ros[i] == 0:
                        g = (c + deltas[i]) & 0xFFFF
                    else:
                        a = ro_off + 2 * i + ros[i] + 2 * (c - starts[i])
                        g = struct.unpack_from(">H", self.d, a)[0]
                        if g:
                            g = (g + deltas[i]) & 0xFFFF
                    self.cmap[c] = g
                    break

    def advance(self, gid):
        hmtx = self.tables["hmtx"][0]
        i = min(gid, self.nhm - 1)
        return struct.unpack_from(">H", self.d, hmtx + 4 * i)[0]

    def glyph_loc(self, gid):
        loca = self.tables["loca"][0]
        glyf = self.tables["glyf"][0]
        if self.loc_long:
            a, b = struct.unpack_from(">II", self.d, loca + 4 * gid)
        else:
            a, b = struct.unpack_from(">HH", self.d, loca + 2 * gid)
            a, b = a * 2, b * 2
        return (glyf + a, b - a)

    def contours(self, gid, dx=0, dy=0):
        """list of contours; each a list of (x, y, on_curve) in font units"""
        off, ln = self.glyph_loc(gid)
        if ln == 0:
            return []
        nc = struct.unpack_from(">h", self.d, off)[0]
        if nc < 0:
            return self._composite(off + 10, dx, dy)
        p = off + 10
        ends = struct.unpack_from(">%dH" % nc, self.d, p)
        p += 2 * nc
        npts = ends[-1] + 1 if nc else 0
        il = struct.unpack_from(">H", self.d, p)[0]
        p += 2 + il
        flags = []
        while len(flags) < npts:
            f = self.d[p]
            p += 1
            flags.append(f)
            if f & 8:
                r = self.d[p]
                p += 1
                flags.extend([f] * r)
        xs, v = [], 0
        for f in flags:
            if f & 2:
                d = self.d[p]
                p += 1
                v += d if f & 16 else -d
            elif not f & 16:
                v += struct.unpack_from(">h", self.d, p)[0]
                p += 2
            xs.append(v)
        ys, v = [], 0
        for f in flags:
            if f & 4:
                d = self.d[p]
                p += 1
                v += d if f & 32 else -d
            elif not f & 32:
                v += struct.unpack_from(">h", self.d, p)[0]
                p += 2
            ys.append(v)
        out, s = [], 0
        for e in ends:
            out.append([(xs[i] + dx, ys[i] + dy, bool(flags[i] & 1)) for i in range(s, e + 1)])
            s = e + 1
        return out

    def _composite(self, p, dx, dy):
        out = []
        while True:
            flags, gi = struct.unpack_from(">HH", self.d, p)
            p += 4
            if flags & 1:
                a1, a2 = struct.unpack_from(">hh", self.d, p)
                p += 4
            else:
                a1, a2 = struct.unpack_from(">bb", self.d, p)
                p += 2
            if flags & 8:
                p += 2
            elif flags & 0x40:
                p += 4
            elif flags & 0x80:
                p += 8
            ox, oy = (a1, a2) if flags & 2 else (0, 0)
            out.extend(self.contours(gi, dx + ox, dy + oy))
            if not flags & 0x20:
                break
        return out


def flatten(contour, scale, steps=6):
    """quadratic B-spline contour -> polygon (list of (x,y)) in pixel units"""
    pts = [(x * scale, y * scale, on) for x, y, on in contour]
    n = len(pts)
    if n == 0:
        return []
    # ensure we start on an on-curve point
    start = next((i for i, p in enumerate(pts) if p[2]), None)
    if start is None:  # all off-curve: synthesize midpoint
        x0, y0, _ = pts[0]
        x1, y1, _ = pts[1 % n]
        pts.insert(1, ((x0 + x1) / 2, (y0 + y1) / 2, True))
        start, n = 1, n + 1
    pts = pts[start:] + pts[:start]
    poly = [(pts[0][0], pts[0][1])]
    i = 1
    prev = pts[0]
    while i <= n:
        cur = pts[i % n]
        if cur[2]:
            poly.append((cur[0], cur[1]))
            prev = cur
            i += 1
        else:
            nxt = pts[(i + 1) % n]
            if nxt[2]:
                end = nxt
                i += 2
            else:
                end = ((cur[0] + nxt[0]) / 2, (cur[1] + nxt[1]) / 2, True)
                i += 1
            for k in range(1, steps + 1):
                t = k / steps
                x = (1 - t) ** 2 * prev[0] + 2 * (1 - t) * t * cur[0] + t * t * end[0]
                y = (1 - t) ** 2 * prev[1] + 2 * (1 - t) * t * cur[1] + t * t * end[1]
                poly.append((x, y))
            prev = end
    return poly


def rasterize(polys, ss=5):
    """non-zero winding scanline fill with vertical supersampling + exact horizontal coverage.
    returns (x0, y0_top, w, h, rows) with y increasing downward from baseline (y0 <= 0 above)."""
    edges = []
    for poly in polys:
        for i in range(len(poly)):
            x0, y0 = poly[i]
            x1, y1 = poly[(i + 1) % len(poly)]
            if y0 == y1:
                continue
            edges.append((x0, y0, x1, y1))
    if not edges:
        return None
    minx = math.floor(min(min(e[0], e[2]) for e in edges))
    maxx = math.ceil(max(max(e[0], e[2]) for e in edges))
    miny = math.floor(min(min(e[1], e[3]) for e in edges))
    maxy = math.ceil(max(max(e[1], e[3]) for e in edges))
    w, h = maxx - minx, maxy - miny
    if w <= 0 or h <= 0:
        return None
    acc = [[0.0] * w for _ in range(h)]
    for row in range(h):
        for s in range(ss):
            sy = miny + row + (s + 0.5) / ss
            xs = []
            for x0, y0, x1, y1 in edges:
                if (y0 <= sy < y1) or (y1 <= sy < y0):
                    t = (sy - y0) / (y1 - y0)
                    xs.append((x0 + t * (x1 - x0), 1 if y1 > y0 else -1))
            xs.sort()
            wind = 0
            for k in range(len(xs)):
                prevw = wind
                wind += xs[k][1]
                if prevw == 0 and wind != 0:
                    xa = xs[k][0]
                elif prevw != 0 and wind == 0:
                    xb = xs[k][0]
                    a, b = xa - minx, xb - minx
                    ia, ib = int(math.floor(a)), int(math.floor(b))
                    if ia == ib:
                        acc[row][ia] += b - a
                    else:
                        acc[row][ia] += ia + 1 - a
                        for px in range(ia + 1, min(ib, w)):
                            acc[row][px] += 1
                        if ib < w:
                            acc[row][ib] += b - ib
    rows = []
    for row in acc:
        rows.append(bytes(min(255, int(round(255 * min(1.0, v / ss) ** 0.85))) for v in row))
    # font y axis is up; convert: top row corresponds to maxy
    rows.reverse()
    return minx, -maxy, w, h, rows


def bake(ttf, px):
    scale = px / ttf.upem
    glyphs, data = [], bytearray()
    for c in range(FIRST, LAST + 1):
        gid = ttf.cmap.get(c, 0)
        adv = ttf.advance(gid) * scale
        polys = [flatten(ct, scale) for ct in ttf.contours(gid)]
        polys = [p for p in polys if len(p) >= 3]
        r = rasterize(polys) if polys else None
        if r is None:
            glyphs.append((0, 0, 0, 0, adv, len(data)))
            continue
        x0, y0, w, h, rows = r
        glyphs.append((w, h, x0, y0, adv, len(data)))
        for row in rows:
            data.extend(row)
    ascent = int(round(ttf.ascender * scale))
    descent = int(round(-ttf.descender * scale))
    return glyphs, bytes(data), ascent, descent


def emit(name, glyphs, data, ascent, descent, px, out):
    out.append("static const Glyph %s_g[%d] = {" % (name, LAST - FIRST + 1))
    line = []
    for (w, h, xo, yo, adv, off) in glyphs:
        # advance stored in 1/4 pixel for sub-pixel accurate layout
        line.append("{%d,%d,%d,%d,%d,%d}" % (w, h, xo, yo, int(round(adv * 4)), off))
        if len(line) == 6:
            out.append("    " + ",".join(line) + ",")
            line = []
    if line:
        out.append("    " + ",".join(line) + ",")
    out.append("};")
    out.append("static const uint8_t %s_px[%d] = {" % (name, max(1, len(data))))
    for i in range(0, len(data), 32):
        out.append("    " + ",".join(str(b) for b in data[i:i + 32]) + ",")
    if not data:
        out.append("    0,")
    out.append("};")
    return "{ %d, %d, %d, %d, %s_g, %s_px }" % (px, ascent, descent, ascent + descent + max(1, px // 6), name, name)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    reg = TTF(open(sys.argv[1], "rb").read())
    bold = TTF(open(sys.argv[2], "rb").read())
    faces = [("ui", reg, 12), ("bold", bold, 12), ("big", reg, 20),
             ("ui2", reg, 24), ("bold2", bold, 24), ("big2", reg, 40),
             ("mid", reg, 15), ("mid2", reg, 30)]
    out = ["/* generated by tools/bakefont.py from DejaVu Sans (Bitstream Vera licence) - do not edit */",
           "#ifndef FONTDATA_H", "#define FONTDATA_H", "#include <stdint.h>",
           "typedef struct { uint8_t w, h; int8_t xo, yo; uint16_t adv4; uint32_t off; } Glyph;",
           "typedef struct { int px, ascent, descent, line_h; const Glyph *g; const uint8_t *data; } Font;"]
    inits = []
    total = 0
    for name, ttf, px in faces:
        glyphs, data, asc, desc = bake(ttf, px)
        total += len(data) + 10 * len(glyphs)
        inits.append(emit(name, glyphs, data, asc, desc, px, out))
        print("  %-6s %2dpx  %6d bytes" % (name, px, len(data)), file=sys.stderr)
    out.append("static const Font fonts[%d] = {" % len(faces))
    for i in inits:
        out.append("    " + i + ",")
    out.append("};")
    out.append("#endif")
    print("\n".join(out))
    print("total font data: %d bytes" % total, file=sys.stderr)


if __name__ == "__main__":
    main()
