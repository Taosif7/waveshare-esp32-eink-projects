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

void plot_screen(int x, int y) {
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) {
    return;
  }
  display.drawPixel(static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
}

void world_to_screen(const Game *game, int x, int y, int *sx, int *sy) {
  camera_world_to_screen(&game->cam, x, y, sx, sy);
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
  int sx = 0;
  int sy = 0;
  world_to_screen(game, x, y, &sx, &sy);
  int stamp = camera_scale(&game->cam, 1);
  if (stamp > 3) {
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

void blit_screen(int x, int y, int w, int h, const uint8_t *bmp) {
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      if (sprite_on(bmp, w, col, row)) {
        plot_screen(x + col, y + row);
      }
    }
  }
}

// Draw a 1-bit sprite centered on a world point. Art size is treated as world pixels
// times scaleN/scaleD, then multiplied by the camera zoom.
void blit_world_sprite(const Game *game, int wx, int wy, int w, int h, const uint8_t *bmp, int scaleN,
                       int scaleD) {
  if (scaleN < 1) {
    scaleN = 1;
  }
  if (scaleD < 1) {
    scaleD = 1;
  }
  int sx = 0;
  int sy = 0;
  world_to_screen(game, wx, wy, &sx, &sy);
  const int dw = camera_scale(&game->cam, (w * scaleN + scaleD / 2) / scaleD);
  const int dh = camera_scale(&game->cam, (h * scaleN + scaleD / 2) / scaleD);
  if (dw < 1 || dh < 1) {
    return;
  }
  const int ox = sx - dw / 2;
  const int oy = sy - dh / 2;
  for (int dy = 0; dy < dh; ++dy) {
    const int row = dy * h / dh;
    for (int dx = 0; dx < dw; ++dx) {
      const int col = dx * w / dw;
      if (sprite_on(bmp, w, col, row)) {
        plot_screen(ox + dx, oy + dy);
      }
    }
  }
}

void blit_anchored(const Game *game, int wx, int wy, int w, int h, const uint8_t *bmp) {
  blit_world_sprite(game, wx, wy, w, h, bmp, 1, 1);
}

// Draw a rotated sprite centered on a world point. worldW/worldH is the
// collision-matched on-screen size in world pixels; artW/artH is the bitmap.
void blit_facing(const Game *game, int wx, int wy, int artW, int artH, const uint8_t *bmp, int32_t c,
                 int32_t s, int worldW, int worldH) {
  int sx = 0;
  int sy = 0;
  world_to_screen(game, wx, wy, &sx, &sy);
  const int sw = camera_scale(&game->cam, worldW);
  const int sh = camera_scale(&game->cam, worldH);
  if (sw < 1 || sh < 1) {
    return;
  }
  const int rad = (sw > sh ? sw : sh) / 2 + 1;
  for (int dy = -rad; dy <= rad; ++dy) {
    for (int dx = -rad; dx <= rad; ++dx) {
      const int lx = static_cast<int>((static_cast<int64_t>(c) * dx - static_cast<int64_t>(s) * dy) >> 16);
      const int ly = static_cast<int>((static_cast<int64_t>(s) * dx + static_cast<int64_t>(c) * dy) >> 16);
      const int col = lx * artW / sw + artW / 2;
      const int row = ly * artH / sh + artH / 2;
      if (col < 0 || row < 0 || col >= artW || row >= artH) {
        continue;
      }
      if (sprite_on(bmp, artW, col, row)) {
        plot_screen(sx + dx, sy + dy);
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

void draw_ground(const Game *game) {
  int sx = 0;
  int sy = 0;
  world_to_screen(game, game->cam.x, kGroundY, &sx, &sy);
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

void draw_pig(const Game *game, int cx, int cy) {
  // Scale the 23×23 art down to the pig's collision diameter.
  blit_world_sprite(game, cx, cy, kPigArtW, kPigArtH, kPigArt, kPigR * 2, kPigArtW);
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
    const int x = static_cast<int>(bit.x >> 16);
    const int y = static_cast<int>(bit.y >> 16);
    blit_world_sprite(game, x, y, w, h, bmp, 1, 1);
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
  blit_facing(game, cx, cy, kBirdArtW, kBirdArtH, kBirdArt, c, s, kBirdR * 2, kBirdR * 2);
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
      draw_pig(game, cx, cy);
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

void bird_bottom(const Game *game, int wx, int wy, int32_t ang, int *sx, int *sy) {
  world_to_screen(game, wx, wy, sx, sy);
  const int32_t c = fxCos(ang);
  const int32_t s = fxSin(ang);
  const int belly = camera_scale(&game->cam, kBirdR);
  *sx += static_cast<int>((static_cast<int64_t>(s) * belly) >> 16);
  *sy += static_cast<int>((static_cast<int64_t>(c) * belly) >> 16);
}

int sling_src_to_screen(const Game *game, int src) {
  // Art is authored at 1:1; display at 7/5 of that, then apply camera zoom.
  return camera_scale(&game->cam, src * 7 / 5);
}

void draw_sling(const Game *game, const Pose &pose) {
  int fx = 0;
  int gy = 0;
  world_to_screen(game, kForkX, kGroundY, &fx, &gy);
  constexpr int kCrotchX = 11;
  constexpr int kBaseRow = 28;
  constexpr int kTipX[2] = {3, 19};
  constexpr int kTipY[2] = {4, 5};
  const int ox = fx - sling_src_to_screen(game, kCrotchX);
  const int oy = gy - sling_src_to_screen(game, kBaseRow + 1) + 1;
  // Inverse-sample the sling into the zoomed screen rect.
  const int dw = sling_src_to_screen(game, kSlingArtW);
  const int dh = sling_src_to_screen(game, kSlingArtH);
  for (int dy = 0; dy < dh; ++dy) {
    const int row = dy * kSlingArtH / dh;
    for (int dx = 0; dx < dw; ++dx) {
      const int col = dx * kSlingArtW / dw;
      if (sprite_on(kSlingArt, kSlingArtW, col, row)) {
        plot_screen(ox + dx, oy + dy);
      }
    }
  }
  if (game->phase == Phase::Aim) {
    int bx = 0;
    int by = 0;
    bird_bottom(game, pose.birdX, pose.birdY, game->angle, &bx, &by);
    line_screen(ox + sling_src_to_screen(game, kTipX[0]), oy + sling_src_to_screen(game, kTipY[0]), bx, by);
    line_screen(ox + sling_src_to_screen(game, kTipX[1]), oy + sling_src_to_screen(game, kTipY[1]), bx, by);
    draw_bird_sprite(game, pose.birdX, pose.birdY, game->angle, false, false, true, pose.pullFrame, 0, 0);
  }
}

void draw_flashes(const Game *game) {
  if (game->puff.alive && game->puff.frame < kPuffN) {
    const int x = static_cast<int>(game->puff.x >> 16);
    const int y = static_cast<int>(game->puff.y >> 16);
    blit_anchored(game, x, y, kPuffW, kPuffH, kPuff[game->puff.frame]);
  }
  for (int i = 0; i < kTrailN; ++i) {
    const Puff &puff = game->trail[i];
    if (!puff.alive || puff.frame >= 5) {
      continue;
    }
    const int tx = static_cast<int>(puff.x >> 16);
    const int ty = static_cast<int>(puff.y >> 16);
    blit_anchored(game, tx, ty, kPuffW, kPuffH, kPuff[puff.frame]);
  }
  for (int i = 0; i < kMaxFlash; ++i) {
    const Flash &flash = game->flashes[i];
    if (!flash.alive) {
      continue;
    }
    if (flash.kind == FlashKind::Star) {
      blit_world_sprite(game, flash.x, flash.y, kPigStarW, kPigStarH, kPigStar, 1, 1);
      continue;
    }
    int frame = flash.frame;
    if (frame >= kBlastN) {
      frame = kBlastN - 1;
    }
    blit_world_sprite(game, flash.x, flash.y, kBlastW, kBlastH, kBlast[frame], 1, 1);
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
  char line[22];
  snprintf(line, sizeof(line), "LEVEL %d %s", game->level + 1, game->won ? "COMPLETE" : "FAILED");
  display.setFont(u8g2_font_5x7_tr);
  const int textW = display.getStrWidth(line);
  const int boxW = textW + 8;
  const int boxX = (kScreenW - boxW) / 2;
  display.setDrawColor(0);
  display.drawBox(boxX, 22, boxW, 18);
  display.setDrawColor(1);
  display.drawFrame(boxX, 22, boxW, 18);
  display.drawStr(boxX + 4, 34, line);
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
  draw_ground(game);
  draw_bodies(game);
  draw_sling(game, pose);
  draw_bits(game);
  draw_flashes(game);
  draw_arc(game, pose);
  draw_hud(game);
  display.sendBuffer();
}
