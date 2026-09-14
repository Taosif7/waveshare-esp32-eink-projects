#!/usr/bin/env python3
"""Convert a PNG (or other Pillow-readable image) to a 1-bit sprite C array.

Dark pixels become ink (bit 1). Light / transparent pixels are skipped (bit 0).

Packed MSB-first, row stride = (width+7)//8 — the format EPD_BlitSprite expects.

Usage:
  python3 tools/png_to_sprite.py assets/ganesha.png SPRITE_GANESHA
  python3 tools/png_to_sprite.py assets/modak.png SPRITE_MODAK --max-width 20 --max-height 22

Paste the printed block into src/sprites_data.h (replacing the placeholder).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("Pillow is required: pip install pillow\n")
    sys.exit(1)


def to_ink(img: Image.Image, threshold: int) -> list[list[int]]:
    img = img.convert("RGBA")
    w, h = img.size
    pix = img.load()
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = pix[x, y]
            if a < 16:
                continue
            luma = (r * 299 + g * 587 + b * 114) // 1000
            if luma <= threshold:
                out[y][x] = 1
    return out


def crop_ink(grid: list[list[int]]) -> list[list[int]]:
    h = len(grid)
    w = len(grid[0])
    xs = [x for y in range(h) for x in range(w) if grid[y][x]]
    ys = [y for y in range(h) for x in range(w) if grid[y][x]]
    if not xs:
        return grid
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    return [row[x0 : x1 + 1] for row in grid[y0 : y1 + 1]]


def fit(grid: list[list[int]], max_w: int | None, max_h: int | None) -> list[list[int]]:
    h = len(grid)
    w = len(grid[0])
    if not max_w and not max_h:
        return grid
    scale = 1.0
    if max_w:
        scale = min(scale, max_w / w)
    if max_h:
        scale = min(scale, max_h / h)
    if scale >= 0.999:
        return grid
    nw = max(1, int(w * scale))
    nh = max(1, int(h * scale))
    img = Image.new("L", (w, h), 255)
    px = img.load()
    for y in range(h):
        for x in range(w):
            if grid[y][x]:
                px[x, y] = 0
    img = img.resize((nw, nh), Image.Resampling.NEAREST)
    px = img.load()
    return [[1 if px[x, y] < 128 else 0 for x in range(nw)] for y in range(nh)]


def pack(grid: list[list[int]]) -> list[int]:
    h = len(grid)
    w = len(grid[0])
    stride = (w + 7) // 8
    data: list[int] = []
    for y in range(h):
        row = [0] * stride
        for x in range(w):
            if grid[y][x]:
                row[x >> 3] |= 0x80 >> (x & 7)
        data.extend(row)
    return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("name", help="C identifier prefix, e.g. SPRITE_GANESHA")
    parser.add_argument("--threshold", type=int, default=160, help="luma <= this becomes ink")
    parser.add_argument("--max-width", type=int, default=0)
    parser.add_argument("--max-height", type=int, default=0)
    parser.add_argument("--no-crop", action="store_true")
    args = parser.parse_args()

    src = Path(args.image)
    img = Image.open(src)
    grid = to_ink(img, args.threshold)
    if not args.no_crop:
        grid = crop_ink(grid)
    max_w = args.max_width or None
    max_h = args.max_height or None
    grid = fit(grid, max_w, max_h)
    h = len(grid)
    w = len(grid[0])
    data = pack(grid)

    print(f"static constexpr uint16_t {args.name}_W = {w};")
    print(f"static constexpr uint16_t {args.name}_H = {h};")
    print(f"static const uint8_t {args.name}_BITS[] = {{")
    line = "  "
    for i, b in enumerate(data):
        line += f"0x{b:02X},"
        if (i + 1) % 16 == 0:
            print(line)
            line = "  "
        else:
            line += " "
    if line.strip():
        print(line.rstrip())
    print("};")
    print(f"\n/* {src.name}: {w}x{h}, {len(data)} bytes, stride {(w + 7) // 8} */", file=sys.stderr)


if __name__ == "__main__":
    main()
