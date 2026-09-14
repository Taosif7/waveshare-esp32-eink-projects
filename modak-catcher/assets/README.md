Source art (copied from `~/Downloads`):

- `ganesha.png` — converted to a 51×62 1-bit catcher (80% of the first in-game size)
- `modak.png` — converted to a 24×22 1-bit falling sprite

Re-convert after replacing the PNGs:

```bash
python3 tools/png_to_sprite.py assets/ganesha.png SPRITE_GANESHA --max-width 64 --max-height 80
python3 tools/png_to_sprite.py assets/modak.png SPRITE_MODAK --max-width 24 --max-height 28
```

Paste the output into `src/sprites_data.h`.
