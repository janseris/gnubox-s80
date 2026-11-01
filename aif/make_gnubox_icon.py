#!/usr/bin/env python3
"""Draws an icon for gnubox (internet over a Bluetooth link) for Series 80 (Nokia 9300 / 9500): a
laptop, the Nokia 9300 open in front (light silver; a wide screen with four soft keys on the
lid's right edge, two hinge cylinders, white keys, a round joystick) and a modern smartphone, with orange radio waves
going from the Nokia to each of them. Original artwork, generated here (no Bluetooth logo); the
same lighting as the Browser 9300 globe (upper left). 8x supersampled.

Writes icon64.bmp + icon64_mask.bmp (64x50), icon25.bmp + icon25_mask.bmp (25x20) and
preview.png. Masks are black where the icon is drawn (the 9300's convention), 1-bit, with edge
pixels blended toward the Desk's light background."""
import math, os, sys
import numpy as np
from PIL import Image

BG = np.array([235, 235, 235], float)
LIGHT = np.array([-0.55, -0.6, 0.58]) / np.linalg.norm([-0.55, -0.6, 0.58])


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def homography(src, dst):
    A = []
    for (x, y), (u, v) in zip(src, dst):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y, -u])
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y, -v])
    _, _, vt = np.linalg.svd(np.array(A, float))
    return vt[-1].reshape(3, 3)


def quad_uv(xs, ys, quad):
    Hm = homography(quad, [(0, 0), (1, 0), (1, 1), (0, 1)])
    p = np.stack([xs, ys, np.ones_like(xs)], -1) @ Hm.T
    u, v = p[..., 0] / p[..., 2], p[..., 1] / p[..., 2]
    return u, v, (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)


def globe(xs, ys, cx, cy, R, small):
    """colour and coverage of a lit globe (oceans, continents, a graticule, a highlight)"""
    nx, ny = (xs - cx) / R, (ys - cy) / R
    r2 = nx * nx + ny * ny
    inside = r2 <= 1
    nz = np.sqrt(np.clip(1 - r2, 0, 1))
    lat = -np.arcsin(np.clip(ny, -1, 1))
    lon = np.arctan2(nx, nz) + 0.4
    diff = np.clip(nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2], 0, 1)
    ocean = np.array([8, 60, 140.]) + (np.array([60, 170, 250.]) - np.array([8, 60, 140.])) * diff[..., None] ** 0.8
    v = np.zeros_like(lon)
    for clon, clat, r in [(-0.6, 0.5, 0.5), (0.5, 0.2, 0.55), (-0.2, -0.45, 0.4)]:
        d = np.arccos(np.clip(np.sin(lat) * math.sin(clat) + np.cos(lat) * math.cos(clat) * np.cos(lon - clon), -1, 1))
        v = np.maximum(v, 1 - d / r)
    g = smooth(0.02, 0.15, v + 0.15 * np.sin(lon * 6) * np.sin(lat * 5))[..., None]
    landc = np.array([25, 95, 35.]) + (np.array([150, 225, 100.]) - np.array([25, 95, 35.])) * diff[..., None] ** 0.8
    col = ocean * (1 - g) + landc * g
    if not small:
        def lines(a, step, width):
            dd = np.abs(((a / step) + 0.5) % 1 - 0.5) * step
            return 1 - smooth(width * 0.5, width, dd)
        grid = np.maximum(lines(lon, math.radians(30), 0.05), lines(lat, math.radians(30), 0.05)) * (0.3 + 0.7 * nz)
        col = col * (1 - 0.3 * grid[..., None]) + np.array([210, 235, 255.]) * 0.3 * grid[..., None]
    Hh = LIGHT + np.array([0, 0, 1.]); Hh /= np.linalg.norm(Hh)
    spec = np.clip(nx * Hh[0] + ny * Hh[1] + nz * Hh[2], 0, 1) ** 35
    col = col + (255 - col) * np.clip(spec * 1.1, 0, 1)[..., None]
    rim = smooth(0.8, 1.0, np.sqrt(r2)) * np.clip(nx * 0.6 + ny * 0.5 + 0.5, 0, 1)
    col = col + (np.array([130, 210, 255.]) - col) * (0.6 * rim)[..., None]
    return col, inside


def lin(c0, c1, t):
    return np.array(c0, float) + (np.array(c1, float) - np.array(c0, float)) * np.clip(t, 0, 1)[..., None]


def put(col, alpha, inside, c):
    return np.where(inside[..., None], c, col), np.maximum(alpha, inside.astype(float))


def hull(points):
    """the convex hull of 2D points (monotone chain), counter-clockwise"""
    pts = sorted(set(points))
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0: lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0: upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def in_poly(xs, ys, poly):
    inside = np.ones_like(xs, bool)
    n = len(poly)
    for i in range(n):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
        inside &= (x1 - x0) * (ys - y0) - (y1 - y0) * (xs - x0) >= 0
    return inside


def slab(col, alpha, xs, ys, front, off, side_c0, side_c1):
    """a thick slab: the faces between the front quad and its copy moved by off (behind it),
    shaded from side_c0 (near the light) to side_c1"""
    back = [(x + off[0], y + off[1]) for x, y in front]
    poly = hull(front + back)
    ins = in_poly(xs, ys, poly)
    cx = np.mean([p[0] for p in poly]); cy = np.mean([p[1] for p in poly])
    span = max(abs(off[0]), abs(off[1]), 1) * 6
    t = np.clip(((xs - cx) + (ys - cy)) / span * 0.5 + 0.5, 0, 1)
    sc = lin(side_c0, side_c1, t ** 0.8)
    return put(col, alpha, ins, sc * 0.92)


def bevel(c, u, v, wu, wv, light=0.45, dark=0.40):
    """a light edge at the top and left, a dark one at the bottom and right (the light is upper left)"""
    hi = np.maximum(1 - smooth(0, wu, u), 1 - smooth(0, wv, v))
    lo = np.maximum(1 - smooth(0, wu, 1 - u), 1 - smooth(0, wv, 1 - v))
    c = c + (255 - c) * (light * hi)[..., None]
    return c * (1 - dark * lo * (1 - hi))[..., None]


def glass(c, u, v, strength=0.30):
    """a screen under glass: a diagonal glare band and a brighter top"""
    d = u * 0.55 + v
    band = smooth(0.10, 0.18, d) * (1 - smooth(0.36, 0.46, d))
    c = c + (255 - c) * (strength * band + 0.10 * (1 - v))[..., None]
    return c


# ---------------------------------------------------------------- a small 3D scene
# x right, y up, z away from the viewer; boxes made of 6 planar faces, projected orthographically
# from above and to the left, lit from the upper left front, painted far to near.

def rot_x(p, pivot, ang):
    """p (N,3) rotated about the x axis through pivot (y, z) by ang (radians)"""
    q = np.array(p, float)
    dy, dz = q[:, 1] - pivot[0], q[:, 2] - pivot[1]
    c, s_ = math.cos(ang), math.sin(ang)
    q[:, 1] = pivot[0] + dy * c - dz * s_
    q[:, 2] = pivot[1] + dy * s_ + dz * c
    return q


def box(x0, x1, y0, y1, z0, z1):
    """the 8 corners and the 6 faces (corner indices, ordered so (0,0),(1,0),(1,1),(0,1) map to u,v)"""
    P = np.array([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
                  [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], float)
    faces = {
        "top": [3, 2, 6, 7],      # u along x, v from the front (0) to the back (1)
        "front": [3, 2, 1, 0],    # u along x, v from the top (0) down
        "right": [2, 6, 5, 1],
        "left": [7, 3, 0, 4],
        "back": [6, 7, 4, 5],
        "bottom": [0, 1, 5, 4],
    }
    return P, faces


def render(W, H, ss=8):
    small = W < 40
    w, h = W * ss, H * ss
    ys, xs = np.mgrid[0:h, 0:w].astype(float) + 0.5
    col = np.zeros((h, w, 3)) + BG
    alpha = np.zeros((h, w))
    # the camera: looking down 32 degrees, from 24 degrees to the left
    el, az = math.radians(32), math.radians(-24)
    d = np.array([math.sin(az) * math.cos(el), -math.sin(el), math.cos(az) * math.cos(el)])
    up = np.array([0, 1.0, 0])
    r = np.cross(up, d); r /= np.linalg.norm(r)
    uu = np.cross(d, r)
    Ld = np.array([-0.45, 0.85, -0.55]); Ld /= np.linalg.norm(Ld)
    Hd = Ld - d; Hd /= np.linalg.norm(Hd)

    # ---- textures: a face's own colour at (u, v) before lighting; "emit" faces are screens
    def silver(u, v):
        return lin([200, 204, 212], [242, 244, 248], 1 - 0.3 * u - 0.2 * v)

    def nokia_keys(u, v):
        c = lin([185, 190, 198], [215, 218, 225], 1 - 0.3 * u)
        rows, cols_ = (5, 13) if not small else (3, 7)
        area = (u > 0.04) & (u < 0.84) & (v > 0.10) & (v < 0.90)
        ku, kv = ((u - 0.04) / 0.80 * cols_) % 1, ((v - 0.10) / 0.80 * rows) % 1
        keys = area & (ku > 0.10) & (ku < 0.92) & (kv > 0.14) & (kv < 0.90)
        c = np.where(keys[..., None], lin([235, 237, 242], [255, 255, 255], kv), c)
        jd = np.hypot((u - 0.92) / 0.045, (v - 0.30) / 0.13)
        c = np.where((jd <= 1)[..., None], lin([175, 180, 190], [255, 255, 255], 1 - (u - 0.92) * 8 + (v - 0.30) * 3), c)
        return c

    def nokia_screen(u, v):
        c = silver(u, v)
        su, sv = (u - 0.04) / 0.77, (v - 0.12) / 0.76
        scr_in = (su > 0) & (su < 1) & (sv > 0) & (sv < 1)
        scr = lin([175, 175, 218], [248, 248, 253], 1 - sv * 0.5 + su * 0.3)
        side = scr_in & ((su < 0.12) | (su > 0.86))
        scr = np.where(side[..., None], lin([150, 150, 205], [200, 200, 235], 1 - sv), scr)
        if not small:
            iu, iv = (su - 0.17) / 0.66 * 4, (sv - 0.18) / 0.64 * 2
            fu, fv = iu % 1, iv % 1
            icon = scr_in & (iu > 0) & (iu < 4) & (iv > 0) & (iv < 2) & (fu > 0.25) & (fu < 0.75) & (fv > 0.15) & (fv < 0.6)
            scr = np.where(icon[..., None], lin([235, 175, 40], [255, 225, 120], 1 - fv), scr)
        bu, bv = (u - 0.85) / 0.12, (v - 0.10) / 0.80
        kv = (bv * 4) % 1
        btn = (bu > 0) & (bu < 1) & (bv > 0) & (bv < 1) & (kv > 0.12) & (kv < 0.88)
        c = np.where(btn[..., None], lin([200, 204, 212], [252, 252, 254], 1 - kv), c)
        return c, scr_in, glass(scr, su, sv, 0.25)

    def laptop_deck(u, v):
        c = lin([175, 180, 190], [232, 235, 242], 1 - 0.3 * u - 0.3 * v)
        area = (u > 0.07) & (u < 0.93) & (v > 0.45) & (v < 0.92)
        ku, kv = ((u - 0.07) / 0.86 * 14) % 1, ((v - 0.45) / 0.47 * 5) % 1
        keys = area & (ku > 0.12) & (kv > 0.15)
        c = np.where(keys[..., None], lin([40, 42, 50], [75, 78, 88], kv), c)
        pad = (u > 0.36) & (u < 0.64) & (v > 0.08) & (v < 0.36)
        c = np.where(pad[..., None], c * 0.88, c)
        return c

    def laptop_screen(u, v):
        c = lin([25, 27, 33], [60, 63, 72], 1 - v)
        su, sv = (u - 0.05) / 0.90, (v - 0.07) / 0.84
        scr_in = (su > 0) & (su < 1) & (sv > 0) & (sv < 1)
        scr = lin([15, 70, 165], [115, 205, 255], 1 - 0.7 * sv - 0.3 * su)
        return c, scr_in, glass(scr, su, sv, 0.30)

    def phone_front(u, v):
        c = lin([12, 12, 16], [55, 57, 66], 1 - u * 0.5 - v * 0.3)
        su, sv = (u - 0.06) / 0.88, (v - 0.04) / 0.92
        scr_in = (su > 0) & (su < 1) & (sv > 0) & (sv < 1)
        scr = lin([95, 35, 175], [70, 210, 255], 1 - sv * 0.8 + su * 0.2)
        return c, scr_in, glass(scr, su, sv, 0.35)

    # ---- the scene (stylised sizes: the 9300 is 13.2 x 5.1 cm; the laptop and phone are scaled
    #      down to sit with it)
    objs = []          # (corners, faces, {face: texture}, material colour for the other faces)

    def add(P, faces, tex, mat):
        objs.append((P, faces, tex, mat))

    # the laptop, behind on the left: a deck and an open lid (16:10 screen)
    # (a lid is built lying closed, then turned up about its back edge: its underside, which
    #  faced the deck, becomes the screen side facing the viewer, its front edge the top)
    LX0, LX1 = -15.5, -4.0
    lid_d = (LX1 - LX0) * 0.62
    LZ1 = 13.0
    LZ0 = LZ1 - lid_d * 1.05
    P, f = box(LX0, LX1, 0, 0.6, LZ0, LZ1)
    add(P, f, {"top": laptop_deck}, [150, 155, 165])
    P, f = box(LX0, LX1, 0.6, 1.0, LZ1 - lid_d, LZ1)
    P = rot_x(P, (0.6, LZ1), math.radians(108))
    add(P, f, {"bottom": laptop_screen}, [175, 180, 190])
    # the Nokia 9300, in front: the keyboard half and the lid open at about 110 degrees
    NX0, NX1, NZ0, NZ1 = -6.6, 6.6, -2.0, 3.1
    P, f = box(NX0, NX1, 0, 1.0, NZ0, NZ1)
    add(P, f, {"top": nokia_keys}, [175, 180, 190])
    P, f = box(NX0, NX1, 1.0, 1.85, NZ0, NZ1)
    P = rot_x(P, (1.0, NZ1), math.radians(112))
    add(P, f, {"bottom": nokia_screen}, [190, 194, 202])
    # the phone, on the right, standing and leaning back a little (about 2:1, a 20:9 screen)
    PX0, PW, PH = 8.2, 5.0, 10.2
    P, f = box(PX0, PX0 + PW, 0, PH, 1.0, 1.8)
    P = rot_x(P, (0, 1.8), math.radians(14))
    add(P, f, {"front": phone_front}, [30, 30, 36])

    # ---- the projection, fitted to the canvas (the top 28% left for the waves)
    allp = np.concatenate([o[0] for o in objs])
    sx, sy = allp @ r, -(allp @ uu)
    margin = 0.03
    top_room = 0.13 if not small else 0.15
    scale = min(w * (1 - 2 * margin) / (sx.max() - sx.min()), h * (1 - margin - top_room) / (sy.max() - sy.min()))
    ox = w * 0.5 - scale * (sx.max() + sx.min()) / 2
    oy = h * (1 - margin) - scale * sy.max()

    def proj(p):
        return (ox + scale * (p @ r), oy + scale * (-(p @ uu)))

    # every visible face of every object, far to near
    todo = []
    for P, faces, tex, mat in objs:
        for name, idx in faces.items():
            q = P[idx]
            n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
            # (outward normals: the box faces are ordered so the cross product points out or in;
            #  make it point away from the box's centre)
            if np.dot(n, q.mean(0) - P.mean(0)) < 0: n = -n
            if np.dot(n, d) >= 0: continue                 # facing away
            todo.append((float(q.mean(0) @ d), q, n, name, tex, mat))
    todo.sort(key=lambda t: -t[0])
    for depth, q, n, name, tex, mat in todo:
        quad = [proj(p) for p in q]
        u, v, ins = quad_uv(xs, ys, quad)
        if not ins.any(): continue
        diff = 0.38 + 0.62 * max(0.0, float(n @ Ld))
        spec = max(0.0, float(n @ Hd)) ** 24
        emit = None
        if name in tex:
            res = tex[name](u, v)
            if isinstance(res, tuple):
                base, emit, scr = res
            else:
                base = res
        else:
            base = np.broadcast_to(np.array(mat, float), u.shape + (3,)).copy()
        c = base * diff + (255 - base * diff) * (0.35 * spec)
        if emit is not None:
            c = np.where(emit[..., None], scr * (0.75 + 0.25 * diff), c)
        # the edges: lighter on the lit ones, a fine dark line at the silhouette
        c = bevel(c, u, v, 0.02 if name in ("top", "front") else 0.06, 0.03 if name in ("top", "front") else 0.06,
                  light=0.35, dark=0.30)
        col, alpha = put(col, alpha, ins, np.clip(c, 0, 255))

    # ---- the waves: glowing orange tubes rising from above the Nokia's lid
    lidtop = [proj(p) for p in objs[3][0]]
    ox_w = np.mean([p[0] for p in lidtop]) + w * 0.07       # (a little right: clear of the laptop)
    oy_w = min(p[1] for p in lidtop) - h * 0.03
    rr = np.hypot(xs - ox_w, ys - oy_w)
    ang = np.arctan2(-(ys - oy_w), xs - ox_w)
    sector = smooth(0.0, 0.22, ang - math.radians(25)) * smooth(0.0, 0.22, math.radians(125) - ang)
    radii = [0.08, 0.16, 0.24] if not small else [0.12, 0.26]
    width = 0.034 if not small else 0.07
    waves = np.zeros_like(rr); across = np.zeros_like(rr)
    for k, r_ in enumerate(radii):
        dd = rr / h - r_
        band = (1 - smooth(width * 0.55, width, np.abs(dd))) * (1 - 0.08 * k)
        sel = band > waves
        across = np.where(sel, np.clip(dd / width * 0.5 + 0.5, 0, 1), across)
        waves = np.maximum(waves, band)
    waves = waves * sector * (1 - alpha)
    tube = 0.55 + 0.45 * np.sin(across * math.pi)
    hl = np.clip(1 - np.abs(across - 0.68) / 0.12, 0, 1)
    base_o = lin([200, 70, 0], [255, 185, 40], tube)
    orange = base_o + (np.array([255, 245, 200.]) - base_o) * hl[..., None] * 0.8
    col = col * (1 - waves[..., None]) + orange * waves[..., None]
    alpha = np.maximum(alpha, waves)

    def down(arr):
        return arr.reshape(H, ss, W, ss, *arr.shape[2:]).mean(axis=(1, 3))
    cov = down(alpha)
    premul = down(col * alpha[..., None])
    rgb = np.where(cov[..., None] > 0, premul / np.maximum(cov[..., None], 1e-6), BG)
    rgb = rgb * cov[..., None] + BG * (1 - cov[..., None])
    return np.clip(rgb, 0, 255).astype(np.uint8), cov >= 0.5


def save(W, H, name, outdir):
    rgb, mask = render(W, H)
    Image.fromarray(rgb, "RGB").save(os.path.join(outdir, name + ".bmp"), "BMP")
    m = np.where(mask, 0, 255).astype(np.uint8)
    Image.fromarray(np.stack([m, m, m], -1), "RGB").save(os.path.join(outdir, name + "_mask.bmp"), "BMP")


def preview(outdir):
    tiles = []
    for (W, H, k) in [(64, 50, 5), (25, 20, 12)]:
        rgb, mask = render(W, H)
        for bg in [(235, 235, 235), (30, 40, 60)]:
            b = rgb.copy(); b[~mask] = bg
            tiles.append(Image.fromarray(b).resize((W * k, H * k), Image.NEAREST))
    out = Image.new("RGB", (650, 510), (255, 0, 255))
    out.paste(tiles[0], (0, 0)); out.paste(tiles[1], (330, 0)); out.paste(tiles[2], (10, 260)); out.paste(tiles[3], (340, 260))
    out.save(os.path.join(outdir, "preview.png"))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    for (W, H, n) in [(64, 50, "icon64"), (25, 20, "icon25")]:
        save(W, H, n, out)
    preview(out)
    print("written to", out)
