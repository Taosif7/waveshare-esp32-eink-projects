#!/usr/bin/env python3
"""Generate packed 1-bit placeholder sprites (1=ink, MSB-first)."""

from __future__ import annotations

import math
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "src" / "sprites_data.h"


def new_canvas(w: int, h: int) -> list[list[int]]:
    return [[0] * w for _ in range(h)]


def fill_ellipse(pix: list[list[int]], cx: float, cy: float, rx: float, ry: float) -> None:
    h = len(pix)
    w = len(pix[0])
    for y in range(h):
        for x in range(w):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                pix[y][x] = 1


def fill_rect(pix: list[list[int]], x0: int, y0: int, x1: int, y1: int) -> None:
    h = len(pix)
    w = len(pix[0])
    for y in range(max(0, y0), min(h, y1 + 1)):
        for x in range(max(0, x0), min(w, x1 + 1)):
            pix[y][x] = 1


def fill_poly(pix: list[list[int]], pts: list[tuple[float, float]]) -> None:
    """Scanline fill for a simple polygon."""
    h = len(pix)
    w = len(pix[0])
    n = len(pts)
    for y in range(h):
        xs: list[float] = []
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            if (y0 <= y < y1) or (y1 <= y < y0):
                t = (y - y0) / (y1 - y0)
                xs.append(x0 + t * (x1 - x0))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            a = int(math.ceil(min(xs[i], xs[i + 1])))
            b = int(math.floor(max(xs[i], xs[i + 1])))
            for x in range(max(0, a), min(w, b + 1)):
                pix[y][x] = 1


def pack(pix: list[list[int]]) -> tuple[int, int, list[int]]:
    h = len(pix)
    w = len(pix[0])
    stride = (w + 7) // 8
    data: list[int] = []
    for y in range(h):
        row = [0] * stride
        for x in range(w):
            if pix[y][x]:
                row[x >> 3] |= 0x80 >> (x & 7)
        data.extend(row)
    return w, h, data


def ganesha() -> tuple[int, int, list[int]]:
    w, h = 48, 40
    pix = new_canvas(w, h)
    # Crown
    fill_poly(pix, [(18, 0), (30, 0), (32, 6), (16, 6)])
    fill_rect(pix, 22, 0, 25, 7)
    # Ears
    fill_ellipse(pix, 8, 16, 8, 10)
    fill_ellipse(pix, 39, 16, 8, 10)
    # Head
    fill_ellipse(pix, 24, 16, 11, 10)
    # Trunk
    fill_poly(pix, [(22, 22), (27, 22), (29, 34), (24, 36), (20, 32)])
    # Body
    fill_ellipse(pix, 24, 32, 14, 8)
    # Arms
    fill_ellipse(pix, 8, 30, 7, 4)
    fill_ellipse(pix, 39, 30, 7, 4)
    # Legs
    fill_rect(pix, 14, 35, 21, 39)
    fill_rect(pix, 26, 35, 33, 39)
    return pack(pix)


def modak() -> tuple[int, int, list[int]]:
    w, h = 16, 18
    pix = new_canvas(w, h)
    # Leaf
    fill_ellipse(pix, 8, 3, 2.2, 3.2)
    fill_rect(pix, 7, 5, 8, 6)
    # Dumpling body
    fill_ellipse(pix, 8, 11, 7.2, 6.2)
    fill_ellipse(pix, 8, 13, 6.5, 4.5)
    return pack(pix)


def c_array(name: str, w: int, h: int, data: list[int]) -> str:
    lines = [
        f"static constexpr uint16_t {name}_W = {w};",
        f"static constexpr uint16_t {name}_H = {h};",
        f"static const uint8_t {name}_BITS[] = {{",
    ]
    row = "  "
    for i, b in enumerate(data):
        row += f"0x{b:02X},"
        if (i + 1) % 16 == 0:
            lines.append(row)
            row = "  "
        else:
            row += " "
    if row.strip():
        lines.append(row.rstrip())
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    gw, gh, g = ganesha()
    mw, mh, m = modak()
    text = "\n".join(
        [
            "#pragma once",
            "",
            "#include <stdint.h>",
            "",
            "/* Placeholder 1-bit sprites. bit 1 = black ink, bit 0 = transparent.",
            " * Packed MSB-first, row stride = (W+7)/8.",
            " * Replace these arrays with converted art (see tools/png_to_sprite.py).",
            " */",
            "",
            c_array("SPRITE_GANESHA", gw, gh, g),
            "",
            c_array("SPRITE_MODAK", mw, mh, m),
            "",
        ]
    )
    OUT.write_text(text)
    print(f"wrote {OUT} ganesha={gw}x{gh} modak={mw}x{mh}")


if __name__ == "__main__":
    main()
