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

void plot(const Game *game, int x, int y) {
  const int sx = x - game->camX + shake_x(game);
  const int sy = y + shake_y(game);
  if (sx < 0 || sy < 0 || sx >= kScreenW || sy >= kScreenH) {
    return;
  }
  display.drawPixel(static_cast<u8g2_uint_t>(sx), static_cast<u8g2_uint_t>(sy));
}

void plot_screen(int x, int y) {
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) {
    return;
  }
  display.drawPixel(static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
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

void draw_ground(const Game *game) {
  const int start = (game->camX / kGroundW) * kGroundW - kGroundW;
  for (int x = start; x < game->camX + kScreenW + kGroundW; x += kGroundW) {
    blit_world(game, x, kGroundY, kGroundW, kGroundH, kGround);
  }
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
  if (hit) {
    blit_rot(game, cx, cy, kBirdHitW, kBirdHitH, kBirdHit, ang);
    return;
  }
  if (launch) {
    const int frame = (game->frame / 2) & 1;
    blit_rot(game, cx, cy, kBirdLaunchW, kBirdLaunchH, kBirdLaunch[frame], ang);
    return;
  }
  if (aimBird) {
    blit_rot(game, cx, cy, kBirdPullW, kBirdPullH, kBirdPull[pullFrame], ang);
    return;
  }
  int index = fly_index(vx, vy);
  if (index < 0) {
    index = 0;
  }
  if (index >= kBirdFlyN) {
    index = kBirdFlyN - 1;
  }
  blit_world(game, cx - kBirdFlyW / 2, cy - kBirdFlyH / 2, kBirdFlyW, kBirdFlyH, kBirdFly[index]);
  const int speedPx = static_cast<int>(len_speed(vx, vy) >> 16);
  if (speedPx > 70) {
    const int ux = static_cast<int>((vx / speedPx) >> 16);
    const int uy = static_cast<int>((vy / speedPx) >> 16);
    const uint8_t *trail = (game->frame & 2) != 0 ? kTrail1 : kTrail0;
    const int tw = (game->frame & 2) != 0 ? kTrail1W : kTrail0W;
    const int th = (game->frame & 2) != 0 ? kTrail1H : kTrail0H;
    blit_world(game, cx - ux * 8 - tw / 2, cy - uy * 8 - th / 2, tw, th, trail);
  }
}

void draw_bodies(const Game *game) {
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &body = game->world.bodies[i];
    if (!body.alive || body.mat == Mat::Bird) {
      continue;
    }
    int w = 0;
    int h = 0;
    const uint8_t *bmp = body_sprite(body, &w, &h);
    const int cx = static_cast<int>(body.x >> 16);
    const int cy = static_cast<int>(body.y >> 16);
    blit_rot(game, cx, cy, w, h, bmp, body.ang);
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
  const int frame = pose.slingFrame;
  blit_rot(game, pose.forkX, pose.forkY, kSlingW, kSlingH, kSling[frame], game->angle);
  if (game->phase == Phase::Aim) {
    line_world(game, pose.tipX[0], pose.tipY[0], pose.birdX, pose.birdY);
    line_world(game, pose.tipX[1], pose.tipY[1], pose.birdX, pose.birdY);
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
  draw_ground(game);
  draw_bodies(game);
  draw_sling(game, pose);
  draw_bits(game);
  draw_flashes(game);
  draw_arc(game, pose);
  draw_hud(game);
  display.sendBuffer();
}
