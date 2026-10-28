#include "game.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

#include "input.h"
#include "sprites.h"

namespace {

constexpr int8_t kDir[8][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};

void local_to_world(int ox, int oy, int32_t ang, int lx, int ly, int *x, int *y) {
  const int32_t c = fxCos(ang);
  const int32_t s = fxSin(ang);
  const int32_t sx = fxMul(static_cast<int32_t>(lx) << 16, c) + fxMul(static_cast<int32_t>(ly) << 16, s);
  const int32_t sy = fxMul(static_cast<int32_t>(lx) << 16, -s) + fxMul(static_cast<int32_t>(ly) << 16, c);
  *x = ox + static_cast<int>(sx >> 16);
  *y = oy + static_cast<int>(sy >> 16);
}

void consider_best(Game *game) {
  if (game->score <= game->best) {
    return;
  }
  game->best = game->score;
  Preferences prefs;
  if (!prefs.begin("sling", false)) {
    return;
  }
  prefs.putUInt("best", static_cast<uint32_t>(game->best));
  prefs.end();
}

void load_best(Game *game) {
  Preferences prefs;
  if (!prefs.begin("sling", true)) {
    return;
  }
  game->best = static_cast<int>(prefs.getUInt("best", 0));
  prefs.end();
}

void clear_fx(Game *game) {
  memset(game->bits, 0, sizeof(game->bits));
  memset(&game->puff, 0, sizeof(game->puff));
  memset(game->flashes, 0, sizeof(game->flashes));
}

void add_piece(Game *game, Look look, Mat mat, Shape shape, int x, int y, int hw, int hh, int hp,
               int32_t ang) {
  Spec spec;
  spec.shape = shape;
  spec.mat = mat;
  spec.look = look;
  spec.x = x;
  spec.y = y;
  spec.hw = hw;
  spec.hh = hh;
  spec.hp = hp;
  spec.ang = ang;
  spec.asleep = true;
  world_add(&game->world, spec);
}

void load_level(Game *game) {
  world_clear(&game->world, kGroundY);
  clear_fx(game);
  game->bird = -1;
  game->phase = Phase::Aim;
  game->birdsLeft = 3;
  game->drawing = false;
  game->shake = false;
  game->pullPose = false;
  game->power = 0;
  game->launchT = 0;
  game->hitT = 0;
  game->settle = 0;
  game->won = false;
  game->cleared = false;
  game->shakeT = 0;
  game->camX = 0;
  game->checkpoint = game->score;
  const int32_t up = kOne / 4;
  if (game->level <= 0) {
    game->level = 0;
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 104, 50, 8, 3, 1, up);
    add_piece(game, Look::Pig, Mat::Pig, Shape::Circle, 104, 37, 5, 5, 1, 0);
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 104, 29, 8, 3, 1, 0);
  } else if (game->level == 1) {
    add_piece(game, Look::Stone, Mat::Stone, Shape::Box, 112, 54, 5, 4, 3, 0);
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 112, 47, 8, 3, 1, 0);
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 100, 36, 8, 3, 1, up);
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 124, 36, 8, 3, 1, up);
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 112, 25, 8, 3, 1, 0);
    add_piece(game, Look::Pig, Mat::Pig, Shape::Circle, 112, 39, 5, 5, 1, 0);
    add_piece(game, Look::Pig, Mat::Pig, Shape::Circle, 156, 53, 5, 5, 1, 0);
    add_piece(game, Look::WoodCrate, Mat::Wood, Shape::Box, 156, 44, 4, 4, 1, 0);
  } else {
    game->level = 2;
    add_piece(game, Look::Stone, Mat::Stone, Shape::Box, 118, 54, 5, 4, 3, 0);
    add_piece(game, Look::Stone, Mat::Stone, Shape::Box, 118, 46, 5, 4, 3, 0);
    add_piece(game, Look::Pig, Mat::Pig, Shape::Circle, 118, 37, 5, 5, 1, 0);
    add_piece(game, Look::Glass, Mat::Glass, Shape::Box, 118, 29, 7, 3, 1, 0);
    add_piece(game, Look::WoodPlank, Mat::Wood, Shape::Box, 152, 55, 8, 3, 1, 0);
    add_piece(game, Look::Pig, Mat::Pig, Shape::Circle, 152, 47, 5, 5, 1, 0);
  }
}

bool pigs_left(const Game *game) {
  for (int i = 0; i < kMaxBodies; ++i) {
    const Body &b = game->world.bodies[i];
    if (b.alive && b.mat == Mat::Pig) {
      return true;
    }
  }
  return false;
}

void burst(Game *game, int x, int y, int count, int life, BitKind kind) {
  for (int n = 0; n < count; ++n) {
    Bit *bit = nullptr;
    for (int i = 0; i < kMaxBits; ++i) {
      if (!game->bits[i].alive) {
        bit = &game->bits[i];
        break;
      }
    }
    if (bit == nullptr) {
      break;
    }
    const int dir = static_cast<int>(esp_random() % 8);
    const int spd = 30 + static_cast<int>(esp_random() % 36);
    bit->alive = true;
    bit->x = static_cast<int32_t>(x) << 16;
    bit->y = static_cast<int32_t>(y) << 16;
    bit->vx = static_cast<int32_t>(kDir[dir][0]) * spd * kOne;
    bit->vy = static_cast<int32_t>(kDir[dir][1]) * spd * kOne;
    if (kind == BitKind::Feather) {
      bit->vy -= 24 * kOne;
    }
    bit->life = static_cast<uint8_t>(life);
    bit->kind = kind;
  }
  if (count >= 10) {
    game->shakeMag = 2;
    game->shakeT = 6;
  }
}

void flash_at(Game *game, int x, int y, FlashKind kind) {
  for (int i = 0; i < kMaxFlash; ++i) {
    Flash &flash = game->flashes[i];
    if (flash.alive) {
      continue;
    }
    flash.alive = true;
    flash.x = static_cast<int16_t>(x);
    flash.y = static_cast<int16_t>(y);
    flash.kind = kind;
    flash.frame = 0;
    flash.tick = 0;
    return;
  }
}

BitKind bits_for(Mat mat) {
  if (mat == Mat::Stone) {
    return BitKind::Stone;
  }
  if (mat == Mat::Glass) {
    return BitKind::Glass;
  }
  if (mat == Mat::Pig) {
    return BitKind::Feather;
  }
  return BitKind::Wood;
}

int hit_need(Mat mat) {
  if (mat == Mat::Glass) {
    return 36;
  }
  if (mat == Mat::Pig) {
    return 55;
  }
  if (mat == Mat::Bird) {
    return 70;
  }
  if (mat == Mat::Stone) {
    return 75;
  }
  return 80;
}

void split_wood(Game *game, const Body &parent) {
  if (parent.hw < 6) {
    return;
  }
  const int off = parent.hw / 2;
  const int32_t c = fxCos(parent.ang);
  const int32_t s = fxSin(parent.ang);
  const int dx = static_cast<int>(fxMul(c, static_cast<int32_t>(off) << 16) >> 16);
  const int dy = static_cast<int>(fxMul(-s, static_cast<int32_t>(off) << 16) >> 16);
  const Look look = off >= 6 ? Look::WoodPlank : Look::WoodChip;
  for (int sign = -1; sign <= 1; sign += 2) {
    Spec spec;
    spec.shape = Shape::Box;
    spec.mat = Mat::Wood;
    spec.look = look;
    spec.x = static_cast<int>(parent.x >> 16) + sign * dx;
    spec.y = static_cast<int>(parent.y >> 16) + sign * dy;
    spec.hw = off;
    spec.hh = parent.hh;
    spec.hp = 1;
    spec.ang = parent.ang;
    spec.asleep = false;
    const int id = world_add(&game->world, spec);
    if (id < 0) {
      continue;
    }
    Body &child = game->world.bodies[id];
    const int32_t push = static_cast<int32_t>(sign * 18) << 16;
    child.vx = parent.vx + fxMul(c, push);
    child.vy = parent.vy + fxMul(-s, push);
    child.angVel = parent.angVel + sign * (kOne / 5);
  }
}

void shatter_glass(Game *game, const Body &parent) {
  const int32_t c = fxCos(parent.ang);
  const int32_t s = fxSin(parent.ang);
  for (int sign = -1; sign <= 1; sign += 2) {
    Spec spec;
    spec.shape = Shape::Box;
    spec.mat = Mat::Glass;
    spec.look = Look::GlassChip;
    spec.x = static_cast<int>(parent.x >> 16) + sign * 4;
    spec.y = static_cast<int>(parent.y >> 16);
    spec.hw = 2;
    spec.hh = 2;
    spec.hp = 1;
    spec.ang = parent.ang;
    spec.asleep = false;
    const int id = world_add(&game->world, spec);
    if (id < 0) {
      continue;
    }
    Body &child = game->world.bodies[id];
    const int32_t push = static_cast<int32_t>(sign * 22) << 16;
    child.vx = parent.vx + fxMul(c, push);
    child.vy = parent.vy - 10 * kOne;
    child.angVel = sign * (kOne / 3);
  }
}

void apply_hits(Game *game) {
  bool was[kMaxBodies];
  int hit[kMaxBodies];
  for (int i = 0; i < kMaxBodies; ++i) {
    was[i] = game->world.bodies[i].alive;
    hit[i] = game->world.bodies[i].impact;
    game->world.bodies[i].impact = 0;
  }
  bool broke = false;
  for (int i = 0; i < kMaxBodies; ++i) {
    if (!was[i]) {
      continue;
    }
    Body &body = game->world.bodies[i];
    if (!body.alive || hit[i] < hit_need(body.mat)) {
      continue;
    }
    const int x = static_cast<int>(body.x >> 16);
    const int y = static_cast<int>(body.y >> 16);
    if (body.mat == Mat::Bird) {
      if (i == game->bird) {
        game->hitT = 8;
      }
      burst(game, x, y, 4, 8, BitKind::Feather);
      continue;
    }
    if (body.mat == Mat::Pig) {
      body.alive = false;
      game->score += 100;
      burst(game, x, y, 12, 16, BitKind::Feather);
      flash_at(game, x, y, FlashKind::Blast);
      flash_at(game, x, y, FlashKind::Star);
      broke = true;
      continue;
    }
    if (body.hp > 0) {
      body.hp--;
    }
    if (body.hp > 0) {
      burst(game, x, y, 4, 8, bits_for(body.mat));
      continue;
    }
    const Body saved = body;
    body.alive = false;
    burst(game, x, y, 12, 14, bits_for(saved.mat));
    flash_at(game, x, y, FlashKind::Blast);
    if (saved.mat == Mat::Wood) {
      split_wood(game, saved);
    } else if (saved.mat == Mat::Glass) {
      shatter_glass(game, saved);
    }
    broke = true;
  }
  if (broke) {
    world_wake(&game->world);
  }
}

void launch(Game *game, uint32_t nowMs) {
  if (game->birdsLeft <= 0) {
    return;
  }
  game->pullPose = true;
  Pose pose;
  game_pose(game, &pose);
  game->pullPose = false;

  Spec spec;
  spec.shape = Shape::Circle;
  spec.mat = Mat::Bird;
  spec.look = Look::Bird;
  spec.x = pose.birdX;
  spec.y = pose.birdY;
  spec.hw = 5;
  spec.hh = 5;
  spec.hp = 3;
  spec.ang = game->angle;
  spec.asleep = false;
  const int id = world_add(&game->world, spec);
  if (id < 0) {
    return;
  }
  const int speed = 150 + static_cast<int>((game->power * 70) >> 16);
  Body &bird = game->world.bodies[id];
  bird.vx = fxMul(fxCos(game->angle), static_cast<int32_t>(speed) << 16);
  bird.vy = fxMul(-fxSin(game->angle), static_cast<int32_t>(speed) << 16);
  bird.angVel = kOne / 3;
  game->bird = id;
  game->birdsLeft--;
  game->launchAng = game->angle;
  game->launchT = 8;
  game->hitT = 0;
  game->phase = Phase::Flying;
  game->shotMs = nowMs;
  game->settle = 0;
  game->puff.alive = true;
  game->puff.x = bird.x;
  game->puff.y = bird.y;
  game->puff.vx = -bird.vx / 5;
  game->puff.vy = -bird.vy / 5;
  game->puff.frame = 0;
  game->puff.tick = 0;
  Serial.printf("launch x=%d y=%d speed=%d angle=%ld power=%ld\n", pose.birdX, pose.birdY, speed,
                static_cast<long>(game->angle * 360 / kOne), static_cast<long>(game->power));
}

void finish_shot(Game *game) {
  if (!pigs_left(game)) {
    game->score += 50 * game->birdsLeft;
    game->won = true;
    game->cleared = game->level >= 2;
    game->phase = Phase::Result;
    consider_best(game);
    return;
  }
  if (game->birdsLeft <= 0) {
    game->won = false;
    game->cleared = false;
    game->phase = Phase::Result;
    consider_best(game);
    return;
  }
  game->phase = Phase::Aim;
  game->settle = 0;
}

void move_bits(Game *game) {
  for (int i = 0; i < kMaxBits; ++i) {
    Bit &bit = game->bits[i];
    if (!bit.alive) {
      continue;
    }
    bit.vy += kGravity / 30;
    bit.x += bit.vx / 30;
    bit.y += bit.vy / 30;
    if (bit.life > 0) {
      bit.life--;
    }
    const int x = static_cast<int>(bit.x >> 16);
    const int y = static_cast<int>(bit.y >> 16);
    if (bit.life == 0 || x < -8 || x > kWorldW + 8 || y > kGroundY + 8) {
      bit.alive = false;
    }
  }
  if (game->puff.alive) {
    game->puff.x += game->puff.vx / 30;
    game->puff.y += game->puff.vy / 30;
    game->puff.tick++;
    if (game->puff.tick >= 2) {
      game->puff.tick = 0;
      game->puff.frame++;
      if (game->puff.frame >= 4) {
        game->puff.alive = false;
      }
    }
  }
  for (int i = 0; i < kMaxFlash; ++i) {
    Flash &flash = game->flashes[i];
    if (!flash.alive) {
      continue;
    }
    flash.tick++;
    if (flash.kind == FlashKind::Star) {
      if (flash.tick >= 8) {
        flash.alive = false;
      }
      continue;
    }
    if (flash.tick >= 2) {
      flash.tick = 0;
      flash.frame++;
      if (flash.frame >= 4) {
        flash.alive = false;
      }
    }
  }
}

void update_camera(Game *game) {
  int target = 0;
  if (game->phase == Phase::Flying && game->bird >= 0 && game->world.bodies[game->bird].alive) {
    target = static_cast<int>(game->world.bodies[game->bird].x >> 16) - 48;
  } else if (game->phase != Phase::Aim) {
    target = game->camX;
  }
  if (target < 0) {
    target = 0;
  }
  const int maxCam = kWorldW - kScreenW;
  if (target > maxCam) {
    target = maxCam;
  }
  game->camX += (target - game->camX) / 4;
}

void cull_bird(Game *game) {
  if (game->bird < 0) {
    return;
  }
  Body &bird = game->world.bodies[game->bird];
  if (!bird.alive) {
    game->bird = -1;
    return;
  }
  const int x = static_cast<int>(bird.x >> 16);
  const int y = static_cast<int>(bird.y >> 16);
  if (x < -24 || x > kWorldW + 24 || y > kGroundY + 28) {
    bird.alive = false;
    game->bird = -1;
  }
}

}  // namespace

void game_pose(const Game *game, Pose *pose) {
  int pull = 6;
  int shy = 0;
  if (game->drawing || game->pullPose) {
    pull = 6 + static_cast<int>((game->power * 16) >> 16);
    if ((game->drawing || game->pullPose) && game->shake) {
      const int amp = game->power > (kOne / 2) ? 2 : 1;
      shy = ((game->frame / 2) & 1) != 0 ? amp : -amp;
    }
  }
  local_to_world(kForkX, kForkY, game->angle, -pull, shy, &pose->birdX, &pose->birdY);
  local_to_world(kForkX, kForkY, game->angle, 0, shy, &pose->forkX, &pose->forkY);
  local_to_world(kForkX, kForkY, game->angle, 2, -7 + shy, &pose->tipX[0], &pose->tipY[0]);
  local_to_world(kForkX, kForkY, game->angle, 2, 7 + shy, &pose->tipX[1], &pose->tipY[1]);
  int sling = static_cast<int>((game->power * kSlingN) >> 16);
  if (!game->drawing && !game->pullPose) {
    sling = 0;
  }
  if (sling >= kSlingN) {
    sling = kSlingN - 1;
  }
  if (sling < 0) {
    sling = 0;
  }
  pose->slingFrame = sling;
  int pullFrame = static_cast<int>((game->power * kBirdPullN) >> 16);
  if (pullFrame >= kBirdPullN) {
    pullFrame = kBirdPullN - 1;
  }
  if (pullFrame < 0) {
    pullFrame = 0;
  }
  pose->pullFrame = pullFrame;
}

void game_init(Game *game) {
  memset(game, 0, sizeof(*game));
  game->bird = -1;
  load_best(game);
  input_begin();
  load_level(game);
  Serial.printf("sling-bird best=%d\n", game->best);
}

void game_step(Game *game, uint32_t nowMs) {
  PadInput input;
  input_poll(&input, game->phase == Phase::Aim, nowMs);
  game->angle = input.angle;
  game->frame++;
  if (game->launchT > 0) {
    game->launchT--;
  }
  if (game->hitT > 0) {
    game->hitT--;
  }
  if (game->shakeT > 0) {
    game->shakeT--;
  }

  if (game->phase == Phase::Aim) {
    game->drawing = input.drawing;
    game->shake = input.shake;
    if (input.drawing || input.launch) {
      game->power = input.power;
    } else {
      game->power = 0;
    }
    if (input.launch) {
      launch(game, nowMs);
    }
  } else if (game->phase == Phase::Result) {
    game->drawing = false;
    game->shake = false;
    if (input.padEdge) {
      if (!game->won) {
        game->score = game->checkpoint;
      } else if (game->cleared) {
        game->score = 0;
        game->level = 0;
      } else {
        game->level++;
      }
      load_level(game);
    }
  }

  if (game->phase == Phase::Flying) {
    for (int step = 0; step < 3; ++step) {
      world_step(&game->world);
    }
    apply_hits(game);
    cull_bird(game);
    if (world_asleep(&game->world)) {
      game->settle++;
    } else {
      game->settle = 0;
    }
    if (game->settle > 12 || nowMs - game->shotMs > 6000) {
      finish_shot(game);
    }
  }

  move_bits(game);
  update_camera(game);
}
