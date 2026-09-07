"""Reproduces TextKit/Tools/atlas.py rasterization exactly, then measures.

FreeType light autohint, coverage quantised at 43/128/213, blank edge rows/cols cropped.
"""
import os, sys, json, math
import freetype
import numpy as np

LEVELS = (43, 128, 213)
ASCII = list(range(0x20, 0x7F))

def quantise(cov):
    lv = np.zeros(cov.shape, dtype=np.uint8)
    for i, t in enumerate(LEVELS):
        lv[cov >= t] = i + 1
    return lv

def crop(lv):
    ys, xs = np.nonzero(lv)
    if len(ys) == 0:
        return lv[:0, :0], 0, 0
    return lv[ys.min():ys.max()+1, xs.min():xs.max()+1], int(ys.min()), int(xs.min())

class Rast:
    def __init__(self, path, px):
        self.face = freetype.Face(path)
        self.face.set_pixel_sizes(0, px)
        self.px = px
        self.cache = {}
    def glyph(self, ch):
        """(levels 2d array, top, left, advance) as atlas.py would store it."""
        if ch in self.cache:
            return self.cache[ch]
        f = self.face
        gi = f.get_char_index(ord(ch))
        if gi == 0 and ch != ' ':
            r = None
        else:
            f.load_glyph(gi, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
            g = f.glyph; b = g.bitmap
            if b.rows and b.width:
                cov = np.frombuffer(bytes(b.buffer), dtype=np.uint8).reshape(b.rows, b.pitch)[:, :b.width]
                lv, dy, dx = crop(quantise(cov))
            else:
                lv, dy, dx = np.zeros((0,0), np.uint8), 0, 0
            r = (lv, g.bitmap_top - dy, g.bitmap_left + dx, g.advance.x // 64)
        self.cache[ch] = r
        return r
    def has(self, ch):
        return self.face.get_char_index(ord(ch)) != 0
    def advance(self, s):
        t = 0
        for ch in s:
            g = self.glyph(ch)
            if g is None: return None
            t += g[3]
        return t
    def ink_h(self, ch):
        g = self.glyph(ch)
        return 0 if g is None else g[0].shape[0]
    def atlas_bytes(self, cps=ASCII):
        n = 0; data = 0
        for cp in cps:
            g = self.glyph(chr(cp))
            if g is None: continue
            n += 1
            h, w = g[0].shape
            data += (h*w + 3)//4
        return n, data, data + 12*n

def render_string(r, s):
    """Compose a string into a level array on its baseline; returns (arr, baseline_row)."""
    asc = math.ceil(r.face.size.ascender/64); desc = math.ceil(-r.face.size.descender/64)
    W = r.advance(s) or 1
    H = asc + desc + 4
    base = asc + 2
    out = np.zeros((H, W + 8), np.uint8)
    pen = 2
    for ch in s:
        g = r.glyph(ch)
        if g is None: pen += r.px//2; continue
        lv, top, left, adv = g
        h, w = lv.shape
        y0 = base - top; x0 = pen + left
        if h and w:
            ys, xs = max(0,y0), max(0,x0)
            sub = lv[ys-y0:, xs-x0:]
            hh = min(sub.shape[0], out.shape[0]-ys); ww = min(sub.shape[1], out.shape[1]-xs)
            if hh>0 and ww>0:
                np.maximum(out[ys:ys+hh, xs:xs+ww], sub[:hh,:ww], out=out[ys:ys+hh, xs:xs+ww])
        pen += adv
    return out, base

def partial_fraction(r, chars):
    tot = 0; part = 0
    for ch in chars:
        g = r.glyph(ch)
        if g is None: continue
        lv = g[0]
        ink = lv[lv > 0]
        tot += ink.size
        part += int((ink < 3).sum())
    return part/tot if tot else float('nan')

def solid_fraction(r, chars):
    return 1.0 - partial_fraction(r, chars)

def holes(lv):
    """Count enclosed level-0 regions (counters that survived quantisation)."""
    if lv.size == 0: return 0
    bg = (lv == 0)
    pad = np.pad(bg, 1, constant_values=True)
    # flood from border
    seen = np.zeros_like(pad)
    stack = [(0,0)]
    seen[0,0] = True
    H, W = pad.shape
    while stack:
        y,x = stack.pop()
        for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
            ny,nx = y+dy,x+dx
            if 0<=ny<H and 0<=nx<W and pad[ny,nx] and not seen[ny,nx]:
                seen[ny,nx]=True; stack.append((ny,nx))
    enclosed = pad & ~seen
    # label connected components of enclosed
    lab = np.zeros_like(pad, dtype=int); n=0
    for y in range(H):
        for x in range(W):
            if enclosed[y,x] and lab[y,x]==0:
                n += 1; st=[(y,x)]; lab[y,x]=n
                while st:
                    cy,cx = st.pop()
                    for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
                        ny,nx = cy+dy,cx+dx
                        if 0<=ny<H and 0<=nx<W and enclosed[ny,nx] and lab[ny,nx]==0:
                            lab[ny,nx]=n; st.append((ny,nx))
    return n

def counter_report(r, chars="aeobdgpq469"):
    open_n = 0; tot = 0
    closed = []
    for ch in chars:
        g = r.glyph(ch)
        if g is None: continue
        tot += 1
        if holes(g[0]) >= 1: open_n += 1
        else: closed.append(ch)
    return open_n, tot, "".join(closed)

def stem_profile(r, ch='l'):
    """Column level profile through the middle band of a stem glyph."""
    g = r.glyph(ch)
    if g is None: return None
    lv = g[0]
    if lv.shape[0] < 4: return None
    band = lv[lv.shape[0]//3 : 2*lv.shape[0]//3]
    prof = band.max(axis=0)
    return list(int(v) for v in prof)
