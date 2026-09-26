#!/usr/bin/env python3
"""Pack Chromium 1x T-Rex sprites into a 1-bit C++ source file.

Dark ink (the opaque #535353 body) becomes bit 1. Anti-aliased fringe
and transparent pixels stay 0 so the eye, restart arrow, and background
remain white. The cloud is a light-gray outline, so any opaque pixel is ink.
Pterodactyl frames are drawn to the same 46x40 box the runner collides with.
"""

import os
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ASSETS = os.path.join(ROOT, "assets")
OUT_CPP = os.path.join(ROOT, "src", "sprites.cpp")
OUT_H = os.path.join(ROOT, "src", "sprites.h")


def mask(im, opaque):
    im = im.convert("RGBA")
    w, h = im.size
    out = Image.new("1", (w, h), 0)
    src = im.load()
    dst = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = src[x, y]
            if a > 128 and (opaque or r < 160):
                dst[x, y] = 1
    return out


def crop(im, box):
    return im.crop(box)


def pack(im):
    w, h = im.size
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    px = im.load()
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out), w, h


def c_array(name, data):
    lines = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def draw_bird(wings_up):
    im = Image.new("1", (46, 40), 0)
    d = ImageDraw.Draw(im)
    # Beak and head sit on the runner's collision boxes (beak x=2, head x=10).
    d.polygon([(1, 14), (11, 11), (11, 16)], fill=1)
    d.ellipse((8, 8, 18, 18), fill=1)
    d.rectangle((11, 10, 13, 12), fill=0)
    d.polygon([(14, 14), (34, 15), (38, 18), (34, 22), (15, 22)], fill=1)
    d.polygon([(30, 16), (44, 14), (44, 18), (32, 20)], fill=1)
    if wings_up:
        # Two wing fingers with a notch, the chrome flap-up pose.
        d.polygon([(16, 15), (20, 3), (26, 3), (24, 10), (22, 15)], fill=1)
        d.polygon([(22, 14), (30, 1), (38, 1), (36, 7), (26, 15)], fill=1)
    else:
        d.polygon([(16, 20), (6, 30), (10, 34), (14, 30), (24, 22)], fill=1)
        d.polygon([(22, 21), (28, 34), (36, 34), (38, 30), (28, 22)], fill=1)
    d.line([(20, 22), (18, 28), (22, 28)], fill=1)
    d.line([(26, 22), (28, 28), (24, 28)], fill=1)
    return im


def load_all():
    sprites = []

    def add(name, im):
        data, w, h = pack(im)
        sprites.append((name, data, w, h))
        return im

    trex = mask(Image.open(os.path.join(ASSETS, "1x-trex.png")), False)
    for i, x in enumerate((0, 44, 88, 132, 220)):
        add(f"kTrex{i}", crop(trex, (x, 0, x + 44, 47)))

    small = mask(Image.open(os.path.join(ASSETS, "1x-obstacle-small.png")), False)
    for i, (x, w) in enumerate(((0, 17), (17, 34), (51, 51))):
        add(f"kCactusS{i + 1}", crop(small, (x, 0, x + w, 35)))

    large = mask(Image.open(os.path.join(ASSETS, "1x-obstacle-large.png")), False)
    for i, (x, w) in enumerate(((0, 25), (25, 50), (75, 75))):
        add(f"kCactusL{i + 1}", crop(large, (x, 0, x + w, 50)))

    add("kBird0", draw_bird(True))
    add("kBird1", draw_bird(False))
    add("kCloud", mask(Image.open(os.path.join(ASSETS, "1x-cloud.png")), True))

    horizon = mask(Image.open(os.path.join(ASSETS, "1x-horizon.png")), False)
    add("kHorizon0", crop(horizon, (0, 0, 600, 12)))
    add("kHorizon1", crop(horizon, (600, 0, 1200, 12)))

    text = mask(Image.open(os.path.join(ASSETS, "1x-text.png")), False)
    for i in range(12):
        add(f"kGlyph{i}", crop(text, (i * 10, 0, i * 10 + 10, 13)))
    add("kGameOver", crop(text, (0, 13, 191, 24)))
    add("kRestart", mask(Image.open(os.path.join(ASSETS, "1x-restart.png")), False))
    return sprites


def emit(sprites):
    header = """#pragma once

#include <stdint.h>

// 1-bit sprites. Bit 1 = black ink, bit 0 = transparent.
// T-Rex, cacti, horizon, cloud, digits, and restart are the Chromium
// offline runner 1x assets (BSD-3-Clause), thresholded to the panel.
// Pterodactyl frames are drawn to the runner's 46x40 collision box.

void draw_trex(int16_t x, int16_t y, uint8_t pose);
void draw_cactus_small(int16_t x, int16_t y, uint8_t size);
void draw_cactus_large(int16_t x, int16_t y, uint8_t size);
void draw_bird(int16_t x, int16_t y, uint8_t frame);
void draw_cloud(int16_t x, int16_t y);
void draw_horizon(int16_t x, int16_t y, uint8_t variant);
void draw_glyph(int16_t x, int16_t y, uint8_t glyph);
void draw_game_over(int16_t x, int16_t y);
void draw_restart(int16_t x, int16_t y);
"""
    body = [
        '#include "sprites.h"',
        "",
        '#include "port_display.h"',
        "",
    ]
    for name, data, w, h in sprites:
        body.append(c_array(name, data))
        body.append("")
    body.append(
        """static void blit(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *bits) {
  EPD_BlitSprite(x, y, w, h, bits);
}

void draw_trex(int16_t x, int16_t y, uint8_t pose) {
  static const uint8_t *kTable[] = {kTrex0, kTrex1, kTrex2, kTrex3, kTrex4};
  if (pose > 4) {
    pose = 0;
  }
  blit(x, y, 44, 47, kTable[pose]);
}

void draw_cactus_small(int16_t x, int16_t y, uint8_t size) {
  static const uint8_t *kTable[] = {kCactusS1, kCactusS2, kCactusS3};
  static const uint16_t kW[] = {17, 34, 51};
  if (size < 1) {
    size = 1;
  }
  if (size > 3) {
    size = 3;
  }
  blit(x, y, kW[size - 1], 35, kTable[size - 1]);
}

void draw_cactus_large(int16_t x, int16_t y, uint8_t size) {
  static const uint8_t *kTable[] = {kCactusL1, kCactusL2, kCactusL3};
  static const uint16_t kW[] = {25, 50, 75};
  if (size < 1) {
    size = 1;
  }
  if (size > 3) {
    size = 3;
  }
  blit(x, y, kW[size - 1], 50, kTable[size - 1]);
}

void draw_bird(int16_t x, int16_t y, uint8_t frame) {
  blit(x, y, 46, 40, frame ? kBird1 : kBird0);
}

void draw_cloud(int16_t x, int16_t y) {
  blit(x, y, 46, 14, kCloud);
}

void draw_horizon(int16_t x, int16_t y, uint8_t variant) {
  blit(x, y, 600, 12, variant ? kHorizon1 : kHorizon0);
}

void draw_glyph(int16_t x, int16_t y, uint8_t glyph) {
  static const uint8_t *kTable[] = {
      kGlyph0, kGlyph1, kGlyph2, kGlyph3, kGlyph4, kGlyph5,
      kGlyph6, kGlyph7, kGlyph8, kGlyph9, kGlyph10, kGlyph11,
  };
  if (glyph > 11) {
    return;
  }
  blit(x, y, 10, 13, kTable[glyph]);
}

void draw_game_over(int16_t x, int16_t y) {
  blit(x, y, 191, 11, kGameOver);
}

void draw_restart(int16_t x, int16_t y) {
  blit(x, y, 36, 32, kRestart);
}
"""
    )
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write(header)
    with open(OUT_CPP, "w", encoding="utf-8") as f:
        f.write("\n".join(body))
    print(f"wrote {OUT_H} and {OUT_CPP}")


def preview(sprites):
    by_name = {name: (data, w, h) for name, data, w, h in sprites}

    def unpack(name):
        data, w, h = by_name[name]
        im = Image.new("1", (w, h), 0)
        px = im.load()
        stride = (w + 7) // 8
        for y in range(h):
            for x in range(w):
                if data[y * stride + (x >> 3)] & (0x80 >> (x & 7)):
                    px[x, y] = 1
        return im

    def paste(canvas, name, x, y):
        im = unpack(name)
        canvas.paste(im, (x, y))

    # Bird sheet
    birds = Image.new("1", (100, 44), 0)
    birds.paste(unpack("kBird0"), (2, 2))
    birds.paste(unpack("kBird1"), (52, 2))
    birds.resize((200, 88), Image.NEAREST).convert("L").save("/tmp/dino-birds.png")

    poses = Image.new("1", (44 * 5 + 8, 51), 0)
    for i in range(5):
        poses.paste(unpack(f"kTrex{i}"), (2 + i * 45, 2))
    poses.resize((poses.width * 2, poses.height * 2), Image.NEAREST).convert("L").save(
        "/tmp/dino-poses.png"
    )

    # Gameplay-like frame, same vertical offset the firmware uses.
    off = 48
    scene = Image.new("1", (200, 200), 0)
    paste(scene, "kCloud", 130, 30 + off)
    paste(scene, "kHorizon0", -20, 127 + off)
    paste(scene, "kTrex2", 18, 93 + off)
    paste(scene, "kCactusS1", 150, 105 + off)
    paste(scene, "kBird0", 78, 50 + off)
    # score digits 00128 at the right, HI 00042 at the left
    def digits(canvas, text, x, y):
        for i, ch in enumerate(text):
            glyph = 10 if ch == "H" else 11 if ch == "I" else int(ch)
            canvas.paste(unpack(f"kGlyph{glyph}"), (x + i * 11, y))

    digits(scene, "HI", 6, 6)
    digits(scene, "00042", 30, 6)
    digits(scene, "00128", 140, 6)
    scene.convert("L").save("/tmp/dino-scene.png")

    night = scene.point(lambda p: 255 if p == 0 else 0)
    night.convert("L").save("/tmp/dino-night.png")
    print("previews in /tmp/dino-*.png")


def main():
    sprites = load_all()
    emit(sprites)
    preview(sprites)


if __name__ == "__main__":
    main()
