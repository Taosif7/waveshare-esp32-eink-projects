#!/usr/bin/env python3
"""Turn host preview PBMs into 4x PNGs and looping GIFs."""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
ASSETS = ROOT.parent / "assets"
SCALE = 4

STILLS = {
    "pairing.pbm": "pairing.png",
    "paused.pbm": "paused.png",
    "playing.pbm": "playing.png",
    "late.pbm": "late-tape.png",
    "forward.pbm": "fast-forward.png",
    "rewind.pbm": "rewind.png",
    "volume.pbm": "volume.png",
}


def load_frame(path: Path) -> Image.Image:
    # PBM marks set bits as black. The panel lights those pixels, so invert.
    image = ImageOps.invert(Image.open(path).convert("L"))
    return image.resize((image.width * SCALE, image.height * SCALE), Image.Resampling.NEAREST)


def write_gif(folder: Path, dest: Path, duration: int) -> None:
    frames = [load_frame(path) for path in sorted(folder.glob("*.pbm"))]
    if not frames:
        raise SystemExit(f"no frames in {folder}")
    frames[0].save(
        dest,
        save_all=True,
        append_images=frames[1:],
        duration=duration,
        loop=0,
        optimize=False,
    )


def main() -> None:
    ASSETS.mkdir(exist_ok=True)
    for source, name in STILLS.items():
        image = load_frame(BUILD / source)
        image.save(ASSETS / name)
        print(f"wrote {name} {image.size}")
    write_gif(BUILD / "spin", ASSETS / "reels-spin.gif", 42)
    write_gif(BUILD / "whip", ASSETS / "track-whip.gif", 33)
    print("wrote reels-spin.gif and track-whip.gif")
    subprocess.check_call([sys.executable, str(ROOT / "overlay_diff.py")])


if __name__ == "__main__":
    main()
