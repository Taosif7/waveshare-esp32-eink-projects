#!/usr/bin/env python3
"""Perspective-correct the cassette photo and diff it against our playing frame.

`python3 overlay_diff.py --reference` rebuilds assets/reference-128x64.png
from host/photo3.png (the panel, mapped onto 128x64). The preview makefile
runs the diff half: reference-only pixels in red, ours-only in cyan, pixels
lit in both in white, scaled 4x. It prints the mismatch count.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT.parent / "assets"
SCALE = 4

# Corners of the panel glass in photo3.png, TL TR BR BL. Mapped onto the
# 128x64 framebuffer so the bezel, not the lit artwork, lands on the edges.
PANEL = [(27, 11), (683, 20), (670, 356), (16, 360)]


def homography(src_pts, dst_pts):
    rows = []
    vals = []
    for (x, y), (u, v) in zip(src_pts, dst_pts):
        rows.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        rows.append([0, 0, 0, x, y, 1, -v * x, -v * y])
        vals.extend((u, v))
    solved = np.linalg.solve(np.array(rows, float), np.array(vals, float))
    return np.array(
        [[solved[0], solved[1], solved[2]], [solved[3], solved[4], solved[5]], [solved[6], solved[7], 1.0]]
    )


def warp_panel(photo):
    src = np.asarray(photo).astype(np.float32)
    height, width, _ = src.shape
    matrix = homography([(0, 0), (127, 0), (127, 63), (0, 63)], PANEL)
    ys, xs = np.mgrid[0:64, 0:128]
    dest = np.stack([xs.astype(float), ys.astype(float), np.ones_like(xs, float)], 0).reshape(3, -1)
    mapped = matrix @ dest
    mapped /= mapped[2]
    sx = mapped[0].reshape(64, 128)
    sy = mapped[1].reshape(64, 128)
    x0 = np.floor(sx).astype(int)
    y0 = np.floor(sy).astype(int)
    wx = sx - x0
    wy = sy - y0
    x0c = np.clip(x0, 0, width - 1)
    x1c = np.clip(x0 + 1, 0, width - 1)
    y0c = np.clip(y0, 0, height - 1)
    y1c = np.clip(y0 + 1, 0, height - 1)

    def sample(yy, xx):
        return src[yy, xx]

    return (
        sample(y0c, x0c) * ((1 - wx) * (1 - wy))[..., None]
        + sample(y0c, x1c) * (wx * (1 - wy))[..., None]
        + sample(y1c, x0c) * ((1 - wx) * wy)[..., None]
        + sample(y1c, x1c) * (wx * wy)[..., None]
    )


def threshold_panel(color):
    # Lit OLED pixels are local peaks. A flat threshold keeps the camera bloom
    # and drops the thin lines, so a pixel is on when it is clearly brighter
    # than the blurred panel behind it.
    red, green, blue = color[:, :, 0], color[:, :, 1], color[:, :, 2]
    yellow = (red + green) * 0.5 - blue * 0.3
    blurred = np.asarray(
        Image.fromarray(np.clip(yellow, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(2))
    ).astype(np.float32)
    return (yellow > blurred + 8) & (yellow > 22)


def load_pbm(path):
    data = path.read_bytes()
    rest = data[3:]
    newline = rest.find(b"\n")
    width, height = (int(part) for part in rest[:newline].split())
    raw = rest[newline + 1 :]
    bits = np.zeros((height, width), dtype=bool)
    index = 0
    for y in range(height):
        for x in range(width):
            byte = raw[index + x // 8]
            bits[y, x] = (byte >> (7 - (x & 7))) & 1
        index += (width + 7) // 8
    return bits


def load_ours():
    pbm = ROOT / "build" / "playing.pbm"
    if pbm.exists():
        return load_pbm(pbm)
    png = ASSETS / "playing.png"
    image = Image.open(png).convert("L")
    small = image.resize((128, 64), Image.Resampling.NEAREST)
    return np.asarray(small) > 128


def write_reference():
    photo_path = ROOT / "photo3.png"
    if not photo_path.exists():
        raise SystemExit(f"missing {photo_path}")
    color = warp_panel(Image.open(photo_path))
    bits = threshold_panel(color)
    image = Image.fromarray(np.where(bits, 255, 0).astype(np.uint8), mode="L").convert("1")
    ASSETS.mkdir(exist_ok=True)
    image.save(ASSETS / "reference-128x64.png")
    print(f"reference lit {int(bits.sum())}")
    return bits


def load_reference():
    image = Image.open(ASSETS / "reference-128x64.png").convert("L").resize((128, 64), Image.Resampling.NEAREST)
    return np.asarray(image) > 128


def write_overlay(reference, ours):
    ref_only = reference & ~ours
    ours_only = ours & ~reference
    both = reference & ours
    mismatch = int(ref_only.sum() + ours_only.sum())
    rgb = np.zeros((64, 128, 3), dtype=np.uint8)
    rgb[both] = (255, 255, 255)
    rgb[ref_only] = (220, 48, 48)
    rgb[ours_only] = (32, 190, 255)
    image = Image.fromarray(rgb).resize((128 * SCALE, 64 * SCALE), Image.Resampling.NEAREST)
    image.save(ASSETS / "overlay-diff.png")
    print(
        f"mismatch {mismatch} (reference-only {int(ref_only.sum())}, ours-only {int(ours_only.sum())})"
    )
    return mismatch


def main():
    if "--reference" in sys.argv:
        reference = write_reference()
    else:
        reference = load_reference()
    ours = load_ours()
    write_overlay(reference, ours)


if __name__ == "__main__":
    main()
