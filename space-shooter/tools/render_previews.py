#!/usr/bin/env python3
"""Write src/sprites.h from the pixel art, then render 4x menu/play/game-over PNGs.

The PNGs are composited from the bytes just written to sprites.h and from the
layout constexprs in src/game.h, using the same draw order as src/ui.cpp.
"""

import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "sprites.h"
GAME_H = ROOT / "src" / "game.h"
ASSETS = ROOT / "assets"

GREEN = (190, 245, 210, 255)
BLACK = (0, 0, 0, 255)
SCALE = 4

FONT = {
    " ": "....\n....\n....\n....\n....",
    ">": ".#..\n..#.\n...#\n..#.\n.#..",
    "<": "..#.\n.#..\n#...\n.#..\n..#.",
    "-": "....\n....\n###.\n....\n....",
    "A": ".#..\n#.#.\n###.\n#.#.\n#.#.",
    "B": "###.\n#.#.\n##..\n#.#.\n###.",
    "C": ".##.\n#...\n#...\n#...\n.##.",
    "D": "##..\n#.#.\n#.#.\n#.#.\n##..",
    "E": "###.\n#...\n##..\n#...\n###.",
    "F": "###.\n#...\n##..\n#...\n#...",
    "G": ".##.\n#...\n#.##\n#.#.\n.##.",
    "H": "#.#.\n#.#.\n###.\n#.#.\n#.#.",
    "I": "###.\n.#..\n.#..\n.#..\n###.",
    "J": "..#.\n..#.\n..#.\n#.#.\n.#..",
    "K": "#.#.\n##..\n##..\n#.#.\n#.#.",
    "L": "#...\n#...\n#...\n#...\n###.",
    "M": "#.#.\n###.\n###.\n#.#.\n#.#.",
    "N": "#.#.\n##.#\n#.##\n#.#.\n#.#.",
    "O": ".##.\n#.#.\n#.#.\n#.#.\n.##.",
    "P": "###.\n#.#.\n###.\n#...\n#...",
    "Q": ".##.\n#.#.\n#.#.\n.##.\n...#",
    "R": "###.\n#.#.\n##..\n#.#.\n#.#.",
    "S": ".##.\n#...\n.##.\n...#\n.##.",
    "T": "###.\n.#..\n.#..\n.#..\n.#..",
    "U": "#.#.\n#.#.\n#.#.\n#.#.\n.##.",
    "V": "#.#.\n#.#.\n#.#.\n.#..\n.#..",
    "W": "#.#.\n#.#.\n###.\n###.\n#.#.",
    "X": "#.#.\n#.#.\n.#..\n#.#.\n#.#.",
    "Y": "#.#.\n#.#.\n.#..\n.#..\n.#..",
    "Z": "###.\n..#.\n.#..\n#...\n###.",
    "0": ".##.\n#.#.\n#.#.\n#.#.\n.##.",
    "1": ".#..\n##..\n.#..\n.#..\n###.",
    "2": ".##.\n...#\n..#.\n.#..\n###.",
    "3": "###.\n...#\n.##.\n...#\n###.",
    "4": "#.#.\n#.#.\n###.\n..#.\n..#.",
    "5": "###.\n#...\n.##.\n...#\n.##.",
    "6": ".##.\n#...\n###.\n#.#.\n.##.",
    "7": "###.\n...#\n..#.\n.#..\n.#..",
    "8": ".##.\n#.#.\n.##.\n#.#.\n.##.",
    "9": ".##.\n#.#.\n.###\n...#\n.##.",
}

SPRITES = {
    "saucer": """
......####......
....##....##....
...#........#...
...#.##..##.#...
...#........#...
....#.####.#....
..############..
.##############.
.#.#.#.##.#.#.#.
..############..
...#...##...#...
..#..........#..
.#....#..#....#.
""",
    "saucer_b": """
......####......
....##....##....
...#.#....#.#...
...#.##..##.#...
...#........#...
....#.#..#.#....
..############..
.##############.
.#.#.#.#.##.#.#.
..############..
...#...##...#...
..#..........#..
.#....#..#....#.
""",
    "ship": """
.......#.......
......#.#......
.....#...#.....
....#.....#....
...#..###..#...
...#..#.#..#...
..#...#.#...#..
..#..##.##..#..
.#...#...#...#.
.#..##.#.##..#.
#...#.....#...#
#..##.....##..#
##...#####...##
.#....###....#.
..####...####..
...##.....##...
.....#...#.....
""",
    "ship_b": """
.......#.......
......#.#......
.....#...#.....
....#.....#....
...#..###..#...
...#..#.#..#...
..#...#.#...#..
..#..##.##..#..
.#...#...#...#.
.#..##.#.##..#.
#...#.....#...#
#..##.....##..#
##...#####...##
.#....#.#....#.
..####.#.####..
...##..#..##...
.....#.#.#.....
""",
    "boss": """
........####........
......##....##......
.....#........#.....
....#..##..##..#....
....#..........#....
.....#.######.#.....
......#......#......
...##############...
..################..
.##################.
.#.#.#.##..##.#.#.#.
.##################.
..################..
....#....##....#....
...#.....#..#....#..
..#.....#....#....#.
""",
    "boss_b": """
........####........
......##....##......
.....#.#....#.#.....
....#..##..##..#....
....#..........#....
.....#.#.##.#.#.....
......#......#......
...##############...
..################..
.##################.
#.#.#.##..##.#.#.#.#
.##################.
..################..
....#....##....#....
...#.....#..#....#..
..#.....#....#....#.
""",
    "rock": """
..####...
.######.#
###.#.###
##.###.##
#.##.#.##
.###.###.
..#####..
...###...
""",
    "rock_b": """
...###...
.######..
##.###.##
#.##.#.##
###.###.#
.##.#.###
..######.
...####..
""",
    "heart": """
.#.#.
#####
#####
.###.
..#..
""",
    "spread": """
#.....#
.#...#.
..#.#..
...#...
...#...
...#...
...#...
""",
    "shield": """
..###..
.#...#.
#.....#
#.....#
#.....#
.#...#.
..###..
""",
}

NAMES = {
    "saucer": ("kSaucerW", "kSaucerH", "kSaucer"),
    "saucer_b": ("kSaucerW", "kSaucerH", "kSaucerB"),
    "ship": ("kShipW", "kShipH", "kShip"),
    "ship_b": ("kShipW", "kShipH", "kShipB"),
    "boss": ("kBossW", "kBossH", "kBoss"),
    "boss_b": ("kBossW", "kBossH", "kBossB"),
    "rock": ("kRockW", "kRockH", "kRock"),
    "rock_b": ("kRockW", "kRockH", "kRockB"),
    "heart": ("kHeartW", "kHeartH", "kHeart"),
    "spread": ("kPowerW", "kPowerH", "kSpread"),
    "shield": ("kPowerW", "kPowerH", "kShieldIcon"),
}


def rows_of(art):
    rows = [line for line in art.strip("\n").split("\n")]
    width = len(rows[0])
    if any(len(row) != width for row in rows):
        raise SystemExit("ragged sprite:\n" + art)
    return width, rows


def to_xbm(art):
    width, rows = rows_of(art)
    span = (width + 7) // 8
    data = []
    for row in rows:
        for block in range(span):
            byte = 0
            for bit in range(8):
                x = block * 8 + bit
                if x < width and row[x] == "#":
                    byte |= 1 << bit
            data.append(byte)
    return width, len(rows), bytes(data)


def font_bytes():
    data = []
    for code in range(32, 91):
        art = FONT.get(chr(code), "....\n....\n....\n....\n....")
        width, rows = rows_of(art)
        if width != 4 or len(rows) != 5:
            raise SystemExit(f"glyph {chr(code)!r} is {width}x{len(rows)}")
        _, _, blob = to_xbm(art)
        data.extend(blob)
    return bytes(data)


def c_array(name, blob, per_line):
    lines = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(blob), per_line):
        chunk = ", ".join(f"0x{b:02X}" for b in blob[i : i + per_line])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def write_header():
    parts = [
        "#pragma once",
        "",
        "// Pixel art for the portrait shooter. XBM bit order: LSB is the left pixel.",
        "// Regenerated by tools/render_previews.py.",
        "",
    ]
    seen_dims = set()
    blobs = {}
    for key, art in SPRITES.items():
        width, height, blob = to_xbm(art)
        dim_w, dim_h, c_name = NAMES[key]
        blobs[c_name] = (width, height, blob)
        if dim_w not in seen_dims:
            parts.append(f"constexpr int {dim_w} = {width};")
            parts.append(f"constexpr int {dim_h} = {height};")
            seen_dims.add(dim_w)
        parts.append(c_array(c_name, blob, 2 if width > 8 else 8))
        parts.append("")
    parts.append("constexpr int kFontW = 4;")
    parts.append("constexpr int kFontH = 5;")
    parts.append("constexpr int kFontFirst = 32;")
    parts.append("constexpr int kFontLast = 90;")
    parts.append(c_array("kFont", font_bytes(), 15))
    parts.append("")
    HEADER.write_text("\n".join(parts))
    return blobs


def parse_constexpr(text):
    values = {}
    for name, value in re.findall(r"constexpr int(?:16_t)? (\w+) = (-?\d+);", text):
        values[name] = int(value)
    return values


def parse_arrays(text):
    arrays = {}
    for name, body in re.findall(r"static const uint8_t (\w+)\[\] = \{([^}]*)\};", text, re.S):
        arrays[name] = bytes(int(token, 16) for token in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    return arrays


class Screen:
    def __init__(self, const, arrays):
        self.c = const
        self.a = arrays
        self.w = const["kScreenW"]
        self.h = const["kScreenH"]
        self.pixels = [[False for _ in range(self.w)] for _ in range(self.h)]

    def plot(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.pixels[y][x] = True

    def fill(self, x, y, w, h, on=True):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if 0 <= xx < self.w and 0 <= yy < self.h:
                    self.pixels[yy][xx] = on

    def blit(self, x, y, w, h, blob):
        span = (w + 7) // 8
        for row in range(h):
            dy = y + row
            if dy < 0 or dy >= self.h:
                continue
            bits = blob[row * span : (row + 1) * span]
            for col in range(w):
                if bits[col >> 3] & (1 << (col & 7)):
                    self.plot(x + col, dy)

    def glyph(self, x, y, ch, scale=1):
        if "a" <= ch <= "z":
            ch = chr(ord(ch) - 32)
        code = ord(ch) if ch else 32
        if code < self.c["kFontFirst"] or code > self.c["kFontLast"]:
            code = 32
        font = self.a["kFont"]
        font_h = self.c["kFontH"]
        font_w = self.c["kFontW"]
        base = (code - self.c["kFontFirst"]) * font_h
        for row in range(font_h):
            bits = font[base + row]
            for col in range(font_w):
                if bits & (1 << col):
                    if scale == 1:
                        self.plot(x + col, y + row)
                    else:
                        self.fill(x + col * scale, y + row * scale, scale, scale)

    def text_width(self, text, scale=1):
        n = len(text)
        if n <= 0:
            return 0
        fw = self.c["kFontW"]
        return (n - 1) * (fw * scale + 1) + fw * scale

    def text(self, x, y, text, scale=1):
        fw = self.c["kFontW"]
        for i, ch in enumerate(text):
            self.glyph(x, y, ch, scale)
            if i + 1 < len(text):
                x += fw * scale + 1

    def centered(self, y, text, scale=1):
        width = self.text_width(text, scale)
        x = max(0, (self.w - width) // 2)
        self.text(x, y, text, scale)

    def star(self, x, y, kind, frame, twinkle):
        if kind > 0 and (frame + twinkle) % 11 == 0:
            return
        self.plot(x, y)
        if kind == 0:
            return
        self.plot(x - 1, y)
        self.plot(x + 1, y)
        self.plot(x, y - 1)
        self.plot(x, y + 1)
        if kind == 2 and (frame + twinkle) % 5 != 0:
            self.plot(x - 1, y - 1)
            self.plot(x + 1, y - 1)
            self.plot(x - 1, y + 1)
            self.plot(x + 1, y + 1)

    def stars(self, frame, field):
        for x, y, kind, twinkle in field:
            self.star(x, y, kind, frame, twinkle)

    def saucer(self, x, y, frame):
        blob = self.a["kSaucer"] if (frame // 12) & 1 == 0 else self.a["kSaucerB"]
        self.blit(x, y, self.c["kSaucerW"], self.c["kSaucerH"], blob)

    def chevrons(self, y, ch):
        fw = self.c["kFontW"]
        for x in range(0, self.w - fw + 1, fw):
            self.glyph(x, y, ch, 1)

    def hud(self, score, wave, lives):
        self.fill(0, 0, self.w, self.c["kHudH"], False)
        self.text(0, self.c["kHudTextY"], f"{score:05d}", 1)
        self.text(self.c["kWaveX"], self.c["kHudTextY"], f"{wave:02d}", 1)
        self.blit(self.c["kHeartX"], self.c["kHudTextY"], self.c["kHeartW"], self.c["kHeartH"], self.a["kHeart"])
        self.text(self.c["kLivesX"], self.c["kHudTextY"], str(lives), 1)
        for x in range(0, self.w, 2):
            self.plot(x, self.c["kHudLineY"])

    def save(self, path):
        img = Image.new("RGBA", (self.w * SCALE, self.h * SCALE), BLACK)
        px = img.load()
        for y in range(self.h):
            for x in range(self.w):
                if not self.pixels[y][x]:
                    continue
                for sy in range(SCALE):
                    for sx in range(SCALE):
                        px[x * SCALE + sx, y * SCALE + sy] = GREEN
        img.save(path)


MENU_STARS = [
    (6, 32, 1, 1),
    (54, 30, 2, 2),
    (10, 44, 0, 3),
    (32, 46, 1, 0),
    (58, 44, 0, 4),
    (4, 102, 2, 5),
    (20, 110, 0, 6),
    (36, 104, 1, 1),
    (52, 116, 0, 7),
    (14, 122, 2, 8),
    (46, 124, 1, 3),
    (60, 108, 0, 9),
    (8, 26, 0, 2),
    (56, 26, 0, 4),
]

PLAY_STARS = [
    (6, 12, 1, 1),
    (58, 18, 0, 2),
    (50, 44, 2, 3),
    (8, 46, 0, 4),
    (4, 84, 2, 0),
    (56, 90, 0, 6),
    (10, 98, 1, 7),
    (52, 102, 0, 8),
    (2, 112, 0, 1),
    (58, 118, 2, 2),
    (8, 124, 1, 3),
    (46, 64, 0, 9),
]

OVER_STARS = [
    (6, 8, 1, 1),
    (52, 10, 2, 2),
    (8, 22, 0, 3),
    (56, 24, 1, 4),
    (4, 32, 2, 0),
    (58, 36, 0, 5),
    (6, 48, 0, 6),
    (58, 62, 1, 1),
    (10, 66, 2, 2),
    (48, 68, 0, 3),
    (4, 100, 1, 4),
    (28, 108, 2, 5),
    (50, 116, 0, 6),
    (16, 122, 1, 7),
    (40, 104, 0, 8),
]


def render(const, arrays):
    ASSETS.mkdir(exist_ok=True)
    menu = Screen(const, arrays)
    frame = 4
    menu.stars(frame, MENU_STARS)
    menu.centered(const["kTitleY"], "SPACE", 2)
    menu.centered(const["kTitle2Y"], "SHOOTER", 2)
    menu.saucer((const["kScreenW"] - const["kSaucerW"]) // 2, const["kMenuSaucerY"], frame)
    items = [(const["kItemStartY"], "START"), (const["kItemDiffY"], "DIFFICULTY"),
             (const["kItemScoresY"], "HIGH SCORES"), (const["kItemSoundY"], "SOUND ON")]
    for i, (y, label) in enumerate(items):
        if i == 0:
            menu.glyph(0, y, ">", 1)
        menu.text(const["kMenuTextX"], y, label, 1)
    menu.text(12, const["kItemDiffValueY"], "NORMAL", 1)
    menu.save(ASSETS / "menu.png")

    play = Screen(const, arrays)
    frame = 8
    play.stars(frame, PLAY_STARS)
    sw = const["kSaucerW"]
    sh = const["kSaucerH"]
    # Formation x = 4, two rows. The middle saucer of the top row is debris.
    cells = []
    for row, y in ((0, 14), (1, 29)):
        for col in range(3):
            cells.append((4 + col * const["kStrideX"], y, row, col))
    for x, y, row, col in cells:
        if (row, col) == (0, 1):
            cx = x + sw // 2
            cy = y + sh // 2
            for dx, dy in ((0, 0), (2, -1), (-2, 1), (3, 2), (-3, -2), (1, 3), (-1, -3), (4, 0), (-4, 1)):
                play.plot(cx + dx, cy + dy)
                play.plot(cx + dx + 1, cy + dy)
            continue
        play.saucer(x, y, frame)
    play.saucer(8, 58, frame)
    play.blit(46, 76, const["kRockW"], const["kRockH"], arrays["kRock"])
    play.blit(40, 48, const["kPowerW"], const["kPowerH"], arrays["kSpread"])
    play.fill(31, 92, 1, 4)
    play.fill(27, 74, 1, 4)
    play.fill(22, 86, 2, 3)
    ship_y = const["kScreenH"] - const["kShipH"] - 2
    play.blit(24, ship_y, const["kShipW"], const["kShipH"], arrays["kShip"])
    play.hud(40, 2, 3)
    play.save(ASSETS / "gameplay.png")

    over = Screen(const, arrays)
    frame = 8
    over.stars(frame, OVER_STARS)
    over.saucer((const["kScreenW"] - const["kSaucerW"]) // 2, const["kOverSaucerY"], frame)
    over.centered(const["kOverHiY"], "HI 00040", 1)
    over.centered(const["kOverScoreY"], "SC 00040", 1)
    over.chevrons(const["kOverChevronY"], ">")
    over.centered(const["kOverTitleY"], "GAME OVER", 1)
    over.chevrons(const["kOverChevron2Y"], "<")
    over.save(ASSETS / "gameover.png")
    print(f"wrote {ASSETS / 'menu.png'}")
    print(f"wrote {ASSETS / 'gameplay.png'}")
    print(f"wrote {ASSETS / 'gameover.png'}")


def main():
    write_header()
    text = HEADER.read_text()
    arrays = parse_arrays(text)
    const = parse_constexpr(text)
    const.update(parse_constexpr(GAME_H.read_text()))
    # Round-trip: header bytes must match the art the firmware will compile.
    for key, art in SPRITES.items():
        _, _, blob = to_xbm(art)
        c_name = NAMES[key][2]
        if arrays[c_name] != blob:
            raise SystemExit(f"{c_name} in sprites.h does not match the art")
    if arrays["kFont"] != font_bytes():
        raise SystemExit("font bytes drifted")
    render(const, arrays)
    return 0


if __name__ == "__main__":
    sys.exit(main())
