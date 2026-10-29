#include "draw.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>

#include <U8g2lib.h>

#include "sprites.h"

namespace {

constexpr int kOledSda = 8;
constexpr int kOledScl = 9;

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

int shake_x(const Game *game) {
  if (game->shakeT <= 0) {
    return 0;
  }
  return (game->frame & 1) != 0 ? game->shakeMag : -game->shakeMag;
}

int shake_y(const Game *game) {
  if (game->shakeT <= 0) {
    return 0;
  }
  return (game->frame & 2) != 0 ? 1 : -1;
}

void plot_screen(int x, int y) {
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) {
    return;
  }
  display.drawPixel(static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
}

void world_to_screen(const Game *game, int x, int y, int *sx, int *sy) {
  const int32_t zoom = game->camZoom > 0 ? game->camZoom : kOne;
  *sx = static_cast<int>((static_cast<int64_t>(x - game->camX) * zoom) / kOne) + kScreenW / 2 + shake_x(game);
  *sy = static_cast<int>((static_cast<int64_t>(y - game->camY) * zoom) / kOne) + kScreenH / 2 + shake_y(game);
}

int clip_code(int x, int y) {
  int code = 0;
  if (x < 0) {
    code |= 1;
  } else if (x >= kScreenW) {
    code |= 2;
  }
  if (y < 0) {
    code |= 4;
  } else if (y >= kScreenH) {
    code |= 8;
  }
  return code;
}

bool clip_segment(int *x0, int *y0, int *x1, int *y1) {
  int c0 = clip_code(*x0, *y0);
  int c1 = clip_code(*x1, *y1);
  for (int guard = 0; guard < 8; ++guard) {
    if ((c0 | c1) == 0) {
      return true;
    }
    if ((c0 & c1) != 0) {
      return false;
    }
    const int code = c0 != 0 ? c0 : c1;
    int x = 0;
    int y = 0;
    const int dx = *x1 - *x0;
    const int dy = *y1 - *y0;
    if ((code & 8) != 0 && dy != 0) {
      x = *x0 + dx * (kScreenH - 1 - *y0) / dy;
      y = kScreenH - 1;
    } else if ((code & 4) != 0 && dy != 0) {
      x = *x0 + dx * (0 - *y0) / dy;
      y = 0;
    } else if ((code & 2) != 0 && dx != 0) {
      y = *y0 + dy * (kScreenW - 1 - *x0) / dx;
      x = kScreenW - 1;
    } else if (dx != 0) {
      y = *y0 + dy * (0 - *x0) / dx;
      x = 0;
    } else {
      return false;
    }
    if (code == c0) {
      *x0 = x;
      *y0 = y;
      c0 = clip_code(x, y);
    } else {
      *x1 = x;
      *y1 = y;
      c1 = clip_code(x, y);
    }
  }
  return (c0 | c1) == 0;
}

void line_screen(int x0, int y0, int x1, int y1) {
  if (!clip_segment(&x0, &y0, &x1, &y1)) {
    return;
  }
  int dx = x1 - x0;
  int dy = y1 - y0;
  const int sx = dx < 0 ? -1 : 1;
  const int sy = dy < 0 ? -1 : 1;
  if (dx < 0) {
    dx = -dx;
  }
  if (dy < 0) {
    dy = -dy;
  }
  int err = dx - dy;
  for (int n = 0; n < 200; ++n) {
    plot_screen(x0, y0);
    if (x0 == x1 && y0 == y1) {
      return;
    }
    const int e2 = err * 2;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void plot(const Game *game, int x, int y) {
  const int32_t zoom = game->camZoom > 0 ? game->camZoom : kOne;
  const int sx = static_cast<int>((static_cast<int64_t>(x - game->camX) * zoom) / kOne) + kScreenW / 2 +
                 shake_x(game);
  const int sy = static_cast<int>((static_cast<int64_t>(y - game->camY) * zoom) / kOne) + kScreenH / 2 +
                 shake_y(game);
  int stamp = static_cast<int>((zoom + kOne - 1) / kOne);
  if (stamp < 1) {
    stamp = 1;
  } else if (stamp > 3) {
    stamp = 3;
  }
  for (int dy = 0; dy < stamp; ++dy) {
    for (int dx = 0; dx < stamp; ++dx) {
      plot_screen(sx + dx, sy + dy);
    }
  }
}

bool sprite_on(const uint8_t *bmp, int w, int x, int y) {
  const int span = (w + 7) / 8;
  return (bmp[y * span + (x >> 3)] & (1 << (x & 7))) != 0;
}

void blit_world(const Game *game, int x, int y, int w, int h, const uint8_t *bmp) {
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      if (sprite_on(bmp, w, col, row)) {
        plot(game, x + col, y + row);
      }
    }
  }
}

void blit_screen(int x, int y, int w, int h, const uint8_t *bmp) {
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      if (sprite_on(bmp, w, col, row)) {
        plot_screen(x + col, y + row);
      }
    }
  }
}

void blit_rot(const Game *game, int cx, int cy, int w, int h, const uint8_t *bmp, int32_t ang) {
  int32_t a = ang % kOne;
  if (a < 0) {
    a += kOne;
  }
  if (a < 1200 || a > kOne - 1200) {
    blit_world(game, cx - w / 2, cy - h / 2, w, h, bmp);
    return;
  }
  const int32_t c = fxCos(a);
  const int32_t s = fxSin(a);
  const int rad = (w > h ? w : h) / 2 + 2;
  for (int dy = -rad; dy <= rad; ++dy) {
    for (int dx = -rad; dx <= rad; ++dx) {
      const int32_t lx = fxMul(static_cast<int32_t>(dx) << 16, c) + fxMul(static_cast<int32_t>(dy) << 16, -s);
      const int32_t ly = fxMul(static_cast<int32_t>(dx) << 16, s) + fxMul(static_cast<int32_t>(dy) << 16, c);
      const int ix = static_cast<int>(lx >> 16) + w / 2;
      const int iy = static_cast<int>(ly >> 16) + h / 2;
      if (ix < 0 || iy < 0 || ix >= w || iy >= h) {
        continue;
      }
      if (sprite_on(bmp, w, ix, iy)) {
        plot(game, cx + dx, cy + dy);
      }
    }
  }
}

void line_world(const Game *game, int x0, int y0, int x1, int y1) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  if (dx < 0) {
    dx = -dx;
  }
  if (dy < 0) {
    dy = -dy;
  }
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    plot(game, x0, y0);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = err * 2;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void draw_num(int right, int y, int value) {
  if (value < 0) {
    value = 0;
  }
  if (value > 99999) {
    value = 99999;
  }
  char buf[8];
  const int n = snprintf(buf, sizeof(buf), "%d", value);
  int x = right - n * (kDigitW + 1);
  for (int i = 0; i < n; ++i) {
    const int digit = buf[i] - '0';
    if (digit >= 0 && digit < kDigitN) {
      blit_screen(x, y, kDigitW, kDigitH, kDigit[digit]);
    }
    x += kDigitW + 1;
  }
}

int fly_index(int32_t vx, int32_t vy) {
  const int32_t up = -vy;
  const int32_t ax = fxAbs(vx);
  const int32_t ay = fxAbs(up);
  int sector = 1;
  if (ay * 2 < ax) {
    sector = 0;
  } else if (ax * 2 < ay) {
    sector = 2;
  }
  if (vx >= 0 && up >= 0) {
    return sector;
  }
  if (vx < 0 && up >= 0) {
    return 4 - sector;
  }
  if (vx < 0 && up < 0) {
    return 4 + sector;
  }
  return sector == 0 ? 0 : 8 - sector;
}

const uint8_t *body_sprite(const Body &body, int *w, int *h) {
  switch (body.look) {
    case Look::WoodPlank:
      *w = kWoodPlankW;
      *h = kWoodPlankH;
      return kWoodPlank;
    case Look::WoodCrate:
      *w = kWoodCrateW;
      *h = kWoodCrateH;
      return kWoodCrate;
    case Look::WoodChip:
      *w = kWoodChipW;
      *h = kWoodChipH;
      return kWoodChip;
    case Look::Stone:
      *w = kStoneW;
      *h = kStoneH;
      return kStone;
    case Look::StoneChip:
      *w = kStoneChipW;
      *h = kStoneChipH;
      return kStoneChip;
    case Look::Glass:
      *w = kGlassW;
      *h = kGlassH;
      return kGlass;
    case Look::GlassChip:
      *w = kGlassChipW;
      *h = kGlassChipH;
      return kGlassChip;
    case Look::Pig:
      *w = body.asleep ? kPigW : kPigAngryW;
      *h = body.asleep ? kPigH : kPigAngryH;
      return body.asleep ? kPig : kPigAngry;
    default:
      *w = kBirdIdleW;
      *h = kBirdIdleH;
      return kBirdIdle;
  }
}

bool arc_blocked(const Game *game, int x, int y) {
  if (y >= kGroundY) {
    return true;
  }
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &body = game->world.bodies[i];
    if (!body.alive || body.mat == Mat::Bird) {
      continue;
    }
    const int cx = static_cast<int>(body.x >> 16);
    const int cy = static_cast<int>(body.y >> 16);
    const int rx = body.hw + 3;
    const int ry = body.hh + 3;
    if (x >= cx - rx && x <= cx + rx && y >= cy - ry && y <= cy + ry) {
      return true;
    }
  }
  return false;
}

void draw_idle_aim(const Game *game, const Pose &pose) {
  const int32_t c = fxCos(game->angle);
  const int32_t s = fxSin(game->angle);
  for (int i = 2; i <= 8; ++i) {
    const int x = pose.forkX + static_cast<int>((c * (i * 6)) >> 16);
    const int y = pose.forkY - static_cast<int>((s * (i * 6)) >> 16);
    if ((i & 1) == 0) {
      plot(game, x, y);
    }
  }
}

void draw_arc(const Game *game, const Pose &pose) {
  if (game->phase != Phase::Aim) {
    return;
  }
  if (!game->drawing) {
    draw_idle_aim(game, pose);
    return;
  }
  const int speed = 150 + static_cast<int>((game->power * 70) >> 16);
  const int32_t vx = fxMul(fxCos(game->angle), static_cast<int32_t>(speed) << 16);
  int32_t vy = fxMul(-fxSin(game->angle), static_cast<int32_t>(speed) << 16);
  int32_t x = static_cast<int32_t>(pose.birdX) << 16;
  int32_t y = static_cast<int32_t>(pose.birdY) << 16;
  const int32_t dt = 4200;
  for (int i = 0; i < 16; ++i) {
    vy += fxMul(kGravity, dt);
    x += fxMul(vx, dt);
    y += fxMul(vy, dt);
    const int px = static_cast<int>(x >> 16);
    const int py = static_cast<int>(y >> 16);
    if (arc_blocked(game, px, py) || px > kWorldW) {
      break;
    }
    if ((i & 1) == 0) {
      plot(game, px, py);
    }
  }
}

void fill_disc(const Game *game, int cx, int cy, int r, int holeX, int holeY) {
  const int r2 = r * r;
  for (int y = -r; y <= r; ++y) {
    for (int x = -r; x <= r; ++x) {
      if (x * x + y * y > r2) {
        continue;
      }
      const int dx = x - holeX;
      const int dy = y - holeY;
      if (dx * dx + dy * dy <= 1) {
        continue;
      }
      plot(game, cx + x, cy + y);
    }
  }
}

void draw_sky(const Game *game) {
  const int sunX = 18;
  const int sunY = 16;
  for (int y = -3; y <= 3; ++y) {
    for (int x = -3; x <= 3; ++x) {
      const int d = x * x + y * y;
      if (d >= 7 && d <= 10) {
        plot(game, sunX + x, sunY + y);
      }
    }
  }
}

void draw_ground(const Game *game) {
  int sx = 0;
  int sy = 0;
  world_to_screen(game, game->camX, kGroundY, &sx, &sy);
  if (sy < 0 || sy >= kScreenH) {
    return;
  }
  display.drawHLine(0, static_cast<u8g2_uint_t>(sy), kScreenW);
}

void paint_box(const Game *game, int cx, int cy, int hw, int hh, int32_t ang) {
  if (hw < 1) {
    hw = 1;
  }
  if (hh < 1) {
    hh = 1;
  }
  const int32_t c = fxCos(ang);
  const int32_t s = fxSin(ang);
  int px[4];
  int py[4];
  const int cornerX[4] = {-hw, hw, hw, -hw};
  const int cornerY[4] = {-hh, -hh, hh, hh};
  for (int i = 0; i < 4; ++i) {
    const int lx = cornerX[i];
    const int ly = cornerY[i];
    const int wx = cx + static_cast<int>((static_cast<int64_t>(c) * lx + static_cast<int64_t>(s) * ly) >> 16);
    const int wy = cy + static_cast<int>((static_cast<int64_t>(-s) * lx + static_cast<int64_t>(c) * ly) >> 16);
    world_to_screen(game, wx, wy, &px[i], &py[i]);
  }
  for (int i = 0; i < 4; ++i) {
    const int n = (i + 1) & 3;
    line_screen(px[i], py[i], px[n], py[n]);
  }
}

void draw_pig(const Game *game, int cx, int cy, int r, bool angry) {
  fill_disc(game, cx, cy, r, -1, -1);
  plot(game, cx - 2, cy - r - 1);
  plot(game, cx + 2, cy - r - 1);
  plot(game, cx - r - 1, cy);
  plot(game, cx - r - 1, cy + 1);
  if (angry) {
    plot(game, cx - 3, cy - 2);
    plot(game, cx + 1, cy - 2);
  }
}

void circle_screen(int cx, int cy, int r) {
  int x = r;
  int y = 0;
  int err = 1 - x;
  while (x >= y) {
    plot_screen(cx + x, cy + y);
    plot_screen(cx + y, cy + x);
    plot_screen(cx - y, cy + x);
    plot_screen(cx - x, cy + y);
    plot_screen(cx - x, cy - y);
    plot_screen(cx - y, cy - x);
    plot_screen(cx + y, cy - x);
    plot_screen(cx + x, cy - y);
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

void draw_round_bird(const Game *game, int cx, int cy, int r, int32_t c, int32_t s, bool hit) {
  int sx = 0;
  int sy = 0;
  world_to_screen(game, cx, cy, &sx, &sy);
  const int32_t zoom = game->camZoom > 0 ? game->camZoom : kOne;
  int sr = static_cast<int>((static_cast<int64_t>(hit ? r - 1 : r) * zoom) / kOne);
  if (sr < 2) {
    sr = 2;
  } else if (sr > 6) {
    sr = 6;
  }
  circle_screen(sx, sy, sr);
  const int eye = sr / 2;
  plot_screen(sx + static_cast<int>((static_cast<int64_t>(c) * eye) >> 16),
              sy + static_cast<int>((static_cast<int64_t>(-s) * eye) >> 16));
  const int beak = sr + 2;
  plot_screen(sx + static_cast<int>((static_cast<int64_t>(c) * beak) >> 16),
              sy + static_cast<int>((static_cast<int64_t>(-s) * beak) >> 16));
}

void draw_bits(const Game *game) {
  for (int i = 0; i < kMaxBits; ++i) {
    const Bit &bit = game->bits[i];
    if (!bit.alive) {
      continue;
    }
    const bool big = bit.life > 8;
    const uint8_t *bmp = kPartWoodSmall;
    int w = kPartWoodSmallW;
    int h = kPartWoodSmallH;
    if (bit.kind == BitKind::Wood) {
      bmp = big ? kPartWoodBig : kPartWoodSmall;
      w = big ? kPartWoodBigW : kPartWoodSmallW;
      h = big ? kPartWoodBigH : kPartWoodSmallH;
    } else if (bit.kind == BitKind::Stone) {
      bmp = big ? kPartStoneBig : kPartStoneSmall;
      w = big ? kPartStoneBigW : kPartStoneSmallW;
      h = big ? kPartStoneBigH : kPartStoneSmallH;
    } else if (bit.kind == BitKind::Glass) {
      bmp = big ? kPartGlassBig : kPartGlassSmall;
      w = big ? kPartGlassBigW : kPartGlassSmallW;
      h = big ? kPartGlassBigH : kPartGlassSmallH;
    } else {
      bmp = big ? kFeatherBig : kFeatherSmall;
      w = big ? kFeatherBigW : kFeatherSmallW;
      h = big ? kFeatherBigH : kFeatherSmallH;
    }
    const int x = static_cast<int>(bit.x >> 16) - w / 2;
    const int y = static_cast<int>(bit.y >> 16) - h / 2;
    blit_world(game, x, y, w, h, bmp);
  }
}

int32_t len_speed(int32_t vx, int32_t vy) {
  const int32_t ax = fxAbs(vx);
  const int32_t ay = fxAbs(vy);
  return ax > ay ? ax + ay / 2 : ay + ax / 2;
}

void draw_bird_sprite(const Game *game, int cx, int cy, int32_t ang, bool launch, bool hit, bool aimBird,
                      int pullFrame, int32_t vx, int32_t vy) {
  (void)launch;
  (void)aimBird;
  (void)pullFrame;
  int32_t c = fxCos(ang);
  int32_t s = fxSin(ang);
  const int speedPx = static_cast<int>(len_speed(vx, vy) >> 16);
  if (!aimBird && !hit && speedPx > 15) {
    c = vx / speedPx;
    s = -vy / speedPx;
  }
  draw_round_bird(game, cx, cy, 4, c, s, hit);
}

void draw_bodies(const Game *game) {
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &body = game->world.bodies[i];
    if (!body.alive || body.mat == Mat::Bird) {
      continue;
    }
    const int cx = static_cast<int>(body.x >> 16);
    const int cy = static_cast<int>(body.y >> 16);
    if (body.mat == Mat::Pig) {
      draw_pig(game, cx, cy, body.hw < 3 ? 3 : body.hw, !body.asleep);
      continue;
    }
    paint_box(game, cx, cy, body.hw, body.hh, body.ang);
  }
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &body = game->world.bodies[i];
    if (!body.alive || body.mat != Mat::Bird) {
      continue;
    }
    const int cx = static_cast<int>(body.x >> 16);
    const int cy = static_cast<int>(body.y >> 16);
    const bool current = i == game->bird && game->phase == Phase::Flying;
    draw_bird_sprite(game, cx, cy, current && game->launchT > 0 ? game->launchAng : body.ang,
                     current && game->launchT > 0, current && game->hitT > 0, false, 0, body.vx, body.vy);
  }
}

void draw_sling(const Game *game, const Pose &pose) {
  line_world(game, pose.forkX, kGroundY - 1, pose.forkX, pose.forkY);
  line_world(game, pose.forkX + 1, kGroundY - 1, pose.forkX + 1, pose.forkY);
  line_world(game, pose.forkX, pose.forkY, pose.tipX[0], pose.tipY[0]);
  line_world(game, pose.forkX, pose.forkY, pose.tipX[1], pose.tipY[1]);
  if (game->phase == Phase::Aim) {
    line_world(game, pose.tipX[0], pose.tipY[0], pose.birdX, pose.birdY);
    line_world(game, pose.tipX[1], pose.tipY[1], pose.birdX, pose.birdY);
    line_world(game, pose.tipX[0] + 1, pose.tipY[0], pose.birdX + 1, pose.birdY);
    draw_bird_sprite(game, pose.birdX, pose.birdY, game->angle, false, false, true, pose.pullFrame, 0, 0);
  }
}

void draw_flashes(const Game *game) {
  if (game->puff.alive && game->puff.frame < kPuffN) {
    const int x = static_cast<int>(game->puff.x >> 16);
    const int y = static_cast<int>(game->puff.y >> 16);
    blit_world(game, x - kPuffW / 2, y - kPuffH / 2, kPuffW, kPuffH, kPuff[game->puff.frame]);
  }
  for (int i = 0; i < kMaxFlash; ++i) {
    const Flash &flash = game->flashes[i];
    if (!flash.alive) {
      continue;
    }
    if (flash.kind == FlashKind::Star) {
      blit_world(game, flash.x - kPigStarW / 2, flash.y - kPigStarH / 2, kPigStarW, kPigStarH, kPigStar);
      continue;
    }
    int frame = flash.frame;
    if (frame >= kBlastN) {
      frame = kBlastN - 1;
    }
    blit_world(game, flash.x - kBlastW / 2, flash.y - kBlastH / 2, kBlastW, kBlastH, kBlast[frame]);
  }
}

void draw_hud(const Game *game) {
  const int hearts = game->birdsLeft < 0 ? 0 : game->birdsLeft;
  for (int i = 0; i < hearts && i < 3; ++i) {
    blit_screen(2 + i * (kHeartW + 1), 0, kHeartW, kHeartH, kHeart);
  }
  blit_screen(2 + 3 * (kHeartW + 1), 1, kDigitW, kDigitH, kDigit[game->level + 1]);
  draw_num(kScreenW - 2, 0, game->score);
  if (game->phase != Phase::Result) {
    return;
  }
  display.setDrawColor(0);
  display.drawBox(32, 16, 64, 36);
  display.setDrawColor(1);
  display.drawFrame(32, 16, 64, 36);
  if (game->won) {
    blit_screen(58, 20, kCheckW, kCheckH, kCheck);
  } else {
    blit_screen(58, 20, kCrossW, kCrossH, kCross);
  }
  draw_num(96, 32, game->score);
  draw_num(96, 42, game->best);
}

bool device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool display_begin() {
  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(400000);
  uint8_t addr = 0;
  for (int attempt = 0; attempt < 40 && addr == 0; ++attempt) {
    if (device_present(0x3C)) {
      addr = 0x3C;
    } else if (device_present(0x3D)) {
      addr = 0x3D;
    } else {
      Serial.println("No OLED at 0x3C/0x3D. VCC->3.3 GND->G SDA->8 SCL->9");
      delay(250);
    }
  }
  if (addr == 0) {
    return false;
  }
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  Serial.printf("OLED at 0x%02X\n", addr);
  return true;
}

void draw_game(const Game *game) {
  display.clearBuffer();
  Pose pose;
  game_pose(game, &pose);
  draw_sky(game);
  draw_ground(game);
  draw_bodies(game);
  draw_sling(game, pose);
  draw_bits(game);
  draw_flashes(game);
  draw_arc(game, pose);
  draw_hud(game);
  display.sendBuffer();
}
