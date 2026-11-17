#!/usr/bin/env python3
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: AGPL-3.0-only
"""gen_app_icons -- the source SVGs of the default Etesian theme's app icons: an Etesian line glyph on an accent squircle (n = 2.8),
top-lit, the way the design language draws app icons (HdAppIcon, guide 06 / 06b). The glyph paths are from the design language's
icon set (AGPL-3.0-only, https://github.com/Etesian-design/design src/components/icons.ts: 24 x 24 grid, 2 px round stroke; the licence is
LICENSE + ADDITIONAL-TERMS.md); the accent colours are its per-application accents (docs/02-colour.md). No Nokia artwork.
    python3 tools/gen_app_icons.py <icons.ts> <outdir>
Writes <outdir>/<name>.svg and <outdir>/mapping.txt (application UID -> name; UIDs left 0 are not known yet)."""
import math, os, re, sys

APPS = [  # name, glyph, accent, application UID (0 = to fill in from the image's application list)
    ("phone", "phone", "#62B700", 0xE7016B53), ("messages", "message", "#09A7CC", 0x100058C5), ("contacts", "person", "#007DC9", 0x101F4CCE),
    ("calendar", "calendar", "#EF5500", 0x10005901), ("clock", "clock", "#F77219", 0x10005903), ("music", "music", "#CC09BA", 0x102072C3), ("gallery", "photo", "#821DDE", 0x200159B2),
    ("settings", "settings", "#4591FF", 0x100058EC), ("browser", "globe", "#4591FF", 0xE7016B20), ("camera", "camera", "#821DDE", 0), ("files", "folder", "#EA650A", 0x101F84EB),
    ("calculator", "grid", "#ED9507", 0x10005902), ("notes", "edit", "#F2B111", 0x10005907), ("email", "mail", "#0067BC", 0),
    ("bluetooth", "bluetooth", "#1C51AF", 0x10005951), ("search", "search", "#4591FF", 0x10282411), ("logs", "list", "#62B700", 0x101F4CD5), ("radio", "speaker", "#FF8500", 0x10207A89)]

import re as _re

def _arc_to_cubics(x1, y1, rx, ry, phi, fa, fs, x2, y2):
    """SVG elliptical arc -> a list of cubic Beziers [(c1x, c1y, c2x, c2y, x, y), ...] (the implementation notes of the SVG spec, F.6)."""
    if rx == 0 or ry == 0 or (x1 == x2 and y1 == y2):
        return [(x1, y1, x2, y2, x2, y2)]
    rx, ry = abs(rx), abs(ry)
    ph = math.radians(phi)
    cp, sp = math.cos(ph), math.sin(ph)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if fa == fs:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx, cy = cp * cxp - sp * cyp + (x1 + x2) / 2, sp * cxp + cp * cyp + (y1 + y2) / 2
    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a
    th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not fs and dth > 0:
        dth -= 2 * math.pi
    elif fs and dth < 0:
        dth += 2 * math.pi
    n = max(1, int(math.ceil(abs(dth) / (math.pi / 2) - 1e-9)))
    step = dth / n
    out = []
    t = 4.0 / 3.0 * math.tan(step / 4)
    a = th1
    for _ in range(n):
        ca, sa, cb, sb = math.cos(a), math.sin(a), math.cos(a + step), math.sin(a + step)
        pts = [(ca - t * sa, sa + t * ca), (cb + t * sb, sb - t * cb), (cb, sb)]
        seg = []
        for ux, uy in pts:
            ux, uy = ux * rx, uy * ry
            seg += [cp * ux - sp * uy + cx, sp * ux + cp * uy + cy]
        out.append(tuple(seg))
        a += step
    return out

def normalize_path(d):
    """A path d string -> absolute M / L / C / Z only (arcs become cubic curves, H / V / relative commands and Q become lines or cubics):
    what the SVG-Tiny encoder of the skin tools accepts everywhere."""
    toks = _re.findall(r'[MmLlHhVvCcSsQqTtAaZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?', d)
    i, out, cmd = 0, [], None
    x = y = sx = sy = 0.0
    lcx = lcy = None
    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v
    def flag():
        nonlocal i
        t = toks[i]
        if len(t) > 1 and t[0] in '01' and not t.startswith('0.'):
            toks[i] = t[1:]
            return float(t[0])
        i += 1
        return float(t)
    while i < len(toks):
        if _re.match(r'[A-Za-z]', toks[i]):
            cmd = toks[i]; i += 1
            if cmd in 'Zz':
                out.append('Z'); x, y = sx, sy
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            nx, ny = num(), num()
            if rel: nx, ny = x + nx, y + ny
            out.append('M%.3f %.3f' % (nx, ny)); x, y, sx, sy = nx, ny, nx, ny
            cmd = 'l' if rel else 'L'
        elif c == 'L':
            nx, ny = num(), num()
            if rel: nx, ny = x + nx, y + ny
            out.append('L%.3f %.3f' % (nx, ny)); x, y = nx, ny
        elif c == 'H':
            nx = num() + (x if rel else 0)
            out.append('L%.3f %.3f' % (nx, y)); x = nx
        elif c == 'V':
            ny = num() + (y if rel else 0)
            out.append('L%.3f %.3f' % (x, ny)); y = ny
        elif c == 'C':
            v = [num() for _ in range(6)]
            if rel: v = [v[0]+x, v[1]+y, v[2]+x, v[3]+y, v[4]+x, v[5]+y]
            out.append('C' + ' '.join('%.3f' % t for t in v)); lcx, lcy = v[2], v[3]; x, y = v[4], v[5]
        elif c == 'S':
            v = [num() for _ in range(4)]
            if rel: v = [v[0]+x, v[1]+y, v[2]+x, v[3]+y]
            c1x, c1y = (2*x - lcx, 2*y - lcy) if lcx is not None else (x, y)
            out.append('C%.3f %.3f %.3f %.3f %.3f %.3f' % (c1x, c1y, v[0], v[1], v[2], v[3])); lcx, lcy = v[0], v[1]; x, y = v[2], v[3]
        elif c == 'Q':
            v = [num() for _ in range(4)]
            if rel: v = [v[0]+x, v[1]+y, v[2]+x, v[3]+y]
            out.append('C%.3f %.3f %.3f %.3f %.3f %.3f' % (x + 2/3*(v[0]-x), y + 2/3*(v[1]-y), v[2] + 2/3*(v[0]-v[2]), v[3] + 2/3*(v[1]-v[3]), v[2], v[3])); x, y = v[2], v[3]
        elif c == 'A':
            rx, ry, phi = num(), num(), num()
            fa, fs = flag(), flag()
            nx, ny = num(), num()
            if rel: nx, ny = x + nx, y + ny
            for seg in _arc_to_cubics(x, y, rx, ry, phi, int(fa), int(fs), nx, ny):
                out.append('C' + ' '.join('%.3f' % t for t in seg))
            x, y = nx, ny
        else:
            raise ValueError('path command ' + cmd)
        if c not in 'CSQ':
            lcx = lcy = None
    return ''.join(out)

def flatten_path(d, steps=4):
    """normalize_path output (M / L / C / Z) -> M / L / Z only: the skin's SVG-Tiny encoder draws straight stroked segments reliably and
    drops stroke-only curves (found on QEMU: the glyphs with curves came out as empty tiles), so every cubic becomes `steps` short lines."""
    out = []
    x = y = 0.0
    for m in _re.finditer(r'([MLCZ])([^MLCZ]*)', d):
        c, args = m.group(1), [float(t) for t in m.group(2).split()]
        if c == 'Z':
            out.append('Z')
        elif c in 'ML':
            x, y = args[0], args[1]
            out.append('%s%.2f %.2f' % (c, x, y))
        else:
            x1, y1, x2, y2, x3, y3 = args
            for i in range(1, steps + 1):
                t = i / steps
                u = 1 - t
                px = u**3 * x + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t**3 * x3
                py = u**3 * y + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t**3 * y3
                out.append('L%.2f %.2f' % (px, py))
            x, y = x3, y3
    return ''.join(out)

def squircle(n=2.8, r=46.0, c=50.0, pts=32):
    out = []
    for i in range(pts):
        a = 2 * math.pi * i / pts
        ca, sa = math.cos(a), math.sin(a)
        out.append((c + r * math.copysign(abs(ca) ** (2 / n), ca), c + r * math.copysign(abs(sa) ** (2 / n), sa)))
    return "M" + " L".join("%.2f %.2f" % p for p in out) + "Z"

def lighten(hexc, f):
    v = [int(hexc[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02X%02X%02X" % tuple(min(255, int(x + (255 - x) * f)) for x in v)

def darken(hexc, f):
    v = [int(hexc[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02X%02X%02X" % tuple(int(x * (1 - f)) for x in v)

def main(ts, out):
    src = open(ts, encoding="utf-8").read()
    glyphs = dict(re.findall(r"^\s*'?([a-z0-9-]+)'?:\s*'([^']*)'", src, re.M))
    os.makedirs(out, exist_ok=True)
    sq = squircle()
    lines = ["# application UID (hex) -> icon name; generated by gen_app_icons.py"]
    for name, glyph, acc, uid in APPS:
        d = flatten_path(normalize_path(glyphs[glyph]))
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">'
               '<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient></defs>'
               '<path d="%s" fill="url(#g)" stroke="%s" stroke-width="1.4"/>'
               '<g transform="translate(26 26) scale(2.0833)" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="%s"/></g></svg>'
               % (lighten(acc, 0.22), darken(acc, 0.18), sq, lighten(acc, 0.45), d))
        open(os.path.join(out, name + ".svg"), "w", encoding="utf-8", newline="\n").write(svg)
        lines.append("%08X %s" % (uid, name))
    open(os.path.join(out, "mapping.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    print("wrote %d icons" % len(APPS))
if __name__ == "__main__": main(sys.argv[1], sys.argv[2])
