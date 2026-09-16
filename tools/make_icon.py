#!/usr/bin/env python3
"""Generates assets/AppIcon.icns: a blue rounded tile with a white magnifying glass.

Each icon size is rendered directly (antialiased with signed distance fields)
rather than downscaled from one big image. Run only when changing the icon:

    python3 tools/make_icon.py
"""
import math, os, shutil, struct, subprocess, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

BLUE_TOP = (72, 138, 235)
BLUE_BOTTOM = (34, 92, 190)
WHITE = (255, 255, 255)


def coverage(dist):
    return max(0.0, min(1.0, 0.5 - dist))


def rounded_rect(px, py, half, radius):
    dx, dy = abs(px) - (half - radius), abs(py) - (half - radius)
    return math.hypot(max(dx, 0.0), max(dy, 0.0)) + min(max(dx, dy), 0.0) - radius


def segment(px, py, ax, ay, bx, by):
    abx, aby = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * abx + (py - ay) * aby) / (abx * abx + aby * aby)))
    return math.hypot(px - (ax + t * abx), py - (ay + t * aby))


def over(dst, src, a):
    return tuple(round(s * a + d * (1 - a)) for d, s in zip(dst, src))


def render(size):
    s = float(size)
    c = s / 2.0
    tile_half, tile_r = s * 0.402, s * 0.225
    lens_cx, lens_cy = c - s * 0.06, c - s * 0.06
    lens_r, ring_w = s * 0.17, s * 0.055
    hx0, hy0 = lens_cx + lens_r * 0.72, lens_cy + lens_r * 0.72
    hx1, hy1 = c + s * 0.24, c + s * 0.24
    handle_w = s * 0.04
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            px, py = x + 0.5 - c, y + 0.5 - c
            tile = coverage(rounded_rect(px, py, tile_half, tile_r))
            t = (y + 0.5) / s
            base = tuple(round(a * (1 - t) + b * t) for a, b in zip(BLUE_TOP, BLUE_BOTTOM))
            rgb = base
            ring = coverage(abs(math.hypot(x + 0.5 - lens_cx, y + 0.5 - lens_cy) - lens_r) - ring_w / 2)
            handle = coverage(segment(x + 0.5, y + 0.5, hx0, hy0, hx1, hy1) - handle_w)
            glass = max(ring, handle)
            rgb = over(rgb, WHITE, glass)
            row += bytes(rgb) + bytes([round(255 * tile)])
        rows.append(bytes(row))
    return rows


def png(size, rows):
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    iconset = os.path.join(ROOT, "assets", "AppIcon.iconset")
    shutil.rmtree(iconset, ignore_errors=True)
    os.makedirs(iconset)
    for base in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            size = base * scale
            name = f"icon_{base}x{base}" + ("@2x" if scale == 2 else "") + ".png"
            with open(os.path.join(iconset, name), "wb") as f:
                f.write(png(size, render(size)))
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(ROOT, "assets", "AppIcon.icns")], check=True)
    shutil.rmtree(iconset)
    print("wrote assets/AppIcon.icns")


if __name__ == "__main__":
    main()
