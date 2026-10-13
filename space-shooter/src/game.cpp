#include "game.h"

#include <Preferences.h>
#include <esp_random.h>

#include "sound.h"
#include "sprites.h"

static_assert(kSaucerW == 12 && kSaucerH == 10, "saucer sprite");
static_assert(kShipW == 15 && kShipH == 17, "ship sprite");

namespace {

constexpr int kSub = 16;
constexpr int kJoySpan = 1400;
constexpr int kMaxV = 32;
constexpr int kPlayerBulletV = -112;
constexpr int kScoreCap = 99999;

constexpr int8_t kSin[32] = {
    0, 3, 6, 9, 12, 14, 16, 17, 18, 17, 16, 14, 12, 9, 6, 3,
    0, -3, -6, -9, -12, -14, -16, -17, -18, -17, -16, -14, -12, -9, -6, -3,
};

constexpr int8_t kDir[8][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};

Preferences prefs;

int rand_n(int n) {
  if (n <= 1) {
    return 0;
  }
  return static_cast<int>(esp_random() % static_cast<uint32_t>(n));
}

int iabs(int v) {
  return v < 0 ? -v : v;
}

bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void add_score(Game *game, int points) {
  game->score += static_cast<uint32_t>(points);
  if (game->score > kScoreCap) {
    game->score = kScoreCap;
  }
}

void save_prefs(const Game *game, bool scores) {
  if (!prefs.begin("shooter", false)) {
    return;
  }
  prefs.putUChar("snd", game->sound_on ? 1 : 0);
  prefs.putUChar("dif", static_cast<uint8_t>(game->diff));
  if (scores) {
    prefs.putUInt("s0", game->top[0]);
    prefs.putUInt("s1", game->top[1]);
    prefs.putUInt("s2", game->top[2]);
  }
  prefs.end();
}

void load_prefs(Game *game) {
  if (!prefs.begin("shooter", true)) {
    return;
  }
  game->sound_on = prefs.getUChar("snd", 1) != 0;
  const uint8_t diff = prefs.getUChar("dif", 1);
  if (diff <= static_cast<uint8_t>(Difficulty::Hard)) {
    game->diff = static_cast<Difficulty>(diff);
  }
  game->top[0] = prefs.getUInt("s0", 0);
  game->top[1] = prefs.getUInt("s1", 0);
  game->top[2] = prefs.getUInt("s2", 0);
  prefs.end();
}

void seed_stars(Game *game) {
  for (int i = 0; i < kMaxStars; ++i) {
    game->stars[i].x = static_cast<uint8_t>(rand_n(kScreenW));
    game->stars[i].y = static_cast<uint8_t>(rand_n(kScreenH));
    game->stars[i].kind = i < 8 ? 0 : (i < 13 ? 1 : 2);
    game->stars[i].twinkle = static_cast<uint8_t>(rand_n(11));
  }
}

void move_stars(Game *game) {
  for (int i = 0; i < kMaxStars; ++i) {
    Star *star = &game->stars[i];
    uint8_t step = 1;
    if (star->kind == 0) {
      if ((game->frame & 1) == 0) {
        continue;
      }
    } else if (star->kind == 2) {
      step = 2;
    }
    const uint16_t next = static_cast<uint16_t>(star->y) + step;
    if (next >= kScreenH) {
      star->y = 0;
      star->x = static_cast<uint8_t>(rand_n(kScreenW));
    } else {
      star->y = static_cast<uint8_t>(next);
    }
  }
}

int clamp_i(int v, int lo, int hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

void stamp_ghost(Game *game, int x, int y) {
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) {
    return;
  }
  Ghost *ghost = &game->ghosts[game->ghost_i];
  ghost->x = static_cast<uint8_t>(x);
  ghost->y = static_cast<uint8_t>(y);
  ghost->life = 3;
  game->ghost_i = static_cast<uint8_t>((game->ghost_i + 1) % kMaxGhosts);
}

void age_ghosts(Game *game) {
  for (int i = 0; i < kMaxGhosts; ++i) {
    if (game->ghosts[i].life > 0) {
      game->ghosts[i].life--;
    }
  }
}

void kick_shake(Game *game, int frames) {
  if (frames <= game->shake) {
    return;
  }
  game->shake = static_cast<uint8_t>(frames);
  game->shake_x = 1;
  game->shake_y = 0;
}

void tick_juice(Game *game) {
  age_ghosts(game);
  if (game->flash > 0) {
    game->flash--;
  }
  if (game->shake == 0) {
    return;
  }
  const int phase = game->shake & 3;
  game->shake_x = (phase & 1) != 0 ? 1 : -1;
  game->shake_y = (phase & 2) != 0 ? 1 : -1;
  game->shake--;
  if (game->shake == 0) {
    game->shake_x = 0;
    game->shake_y = 0;
  }
}

void update_feel(Game *game, const Input *input) {
  if (input->tilt_ok) {
    const int tx = clamp_i(input->tilt_x, -400, 400);
    const int ty = clamp_i(input->tilt_y, -400, 400);
    game->tilt_seen = true;
    game->tilt_miss = 0;
    game->lean_x = static_cast<int16_t>((game->lean_x * 3 + tx) / 4);
    game->lean_y = static_cast<int16_t>((game->lean_y * 3 + ty) / 4);
  } else if (game->tilt_seen) {
    if (game->tilt_miss < 250) {
      game->tilt_miss++;
    }
    if (game->tilt_miss > 8) {
      game->lean_x = static_cast<int16_t>(game->lean_x * 3 / 4);
      game->lean_y = static_cast<int16_t>(game->lean_y * 3 / 4);
    }
  }
  const int src = input->tilt_ok ? (input->tilt_x / 2 + input->axis_x / 16) : (input->axis_x / 8);
  if (game->bank < 0) {
    if (src > -28) {
      game->bank = src > 60 ? 1 : 0;
    }
  } else if (game->bank > 0) {
    if (src < 28) {
      game->bank = src < -60 ? -1 : 0;
    }
  } else if (src < -60) {
    game->bank = -1;
  } else if (src > 60) {
    game->bank = 1;
  }
}

void clear_enemy_shots(Game *game) {
  for (int i = 0; i < kMaxEnemyShots; ++i) {
    game->enemy_shots[i].alive = false;
  }
}

void clear_combat(Game *game) {
  for (int i = 0; i < kMaxPlayerShots; ++i) {
    game->player_shots[i].alive = false;
  }
  clear_enemy_shots(game);
  for (int i = 0; i < kMaxRocks; ++i) {
    game->rocks[i].alive = false;
  }
  for (int i = 0; i < kMaxBits; ++i) {
    game->bits[i].alive = false;
  }
  for (int i = 0; i < kMaxGhosts; ++i) {
    game->ghosts[i].life = 0;
  }
  game->ghost_i = 0;
  for (int i = 0; i < kMaxInvaders; ++i) {
    game->inv[i].state = InvState::Empty;
  }
  game->power_on = false;
  game->boss_on = false;
}

int lives_for(Difficulty diff) {
  if (diff == Difficulty::Easy) {
    return 4;
  }
  if (diff == Difficulty::Hard) {
    return 2;
  }
  return 3;
}

int fire_period(Difficulty diff) {
  if (diff == Difficulty::Easy) {
    return 70;
  }
  if (diff == Difficulty::Hard) {
    return 26;
  }
  return 44;
}

int dive_period(Difficulty diff) {
  if (diff == Difficulty::Easy) {
    return 110;
  }
  if (diff == Difficulty::Hard) {
    return 48;
  }
  return 75;
}

int rock_period(Difficulty diff) {
  if (diff == Difficulty::Easy) {
    return 170;
  }
  if (diff == Difficulty::Hard) {
    return 64;
  }
  return 100;
}

int dive_vy(Difficulty diff) {
  if (diff == Difficulty::Easy) {
    return 14;
  }
  if (diff == Difficulty::Hard) {
    return 26;
  }
  return 20;
}

void burst_at(Game *game, int px, int py, int count, int life) {
  for (int n = 0; n < count; ++n) {
    Bit *bit = nullptr;
    for (int i = 0; i < kMaxBits; ++i) {
      if (!game->bits[i].alive) {
        bit = &game->bits[i];
        break;
      }
    }
    if (bit == nullptr) {
      return;
    }
    const int dir = rand_n(8);
    const int speed = 10 + rand_n(16);
    bit->alive = true;
    bit->x = static_cast<int16_t>(px * kSub);
    bit->y = static_cast<int16_t>(py * kSub);
    bit->vx = static_cast<int16_t>(kDir[dir][0] * speed);
    bit->vy = static_cast<int16_t>(kDir[dir][1] * speed);
    bit->life = static_cast<uint8_t>(life);
  }
  if (count >= 10) {
    kick_shake(game, 6);
  }
}

void inv_pixel(const Game *game, const Invader *inv, int *x, int *y) {
  if (inv->state == InvState::Formed) {
    *x = (game->form_x >> 4) + inv->col * kStrideX;
    *y = (game->form_y >> 4) + inv->row * kStrideY;
  } else {
    *x = inv->x >> 4;
    *y = inv->y >> 4;
  }
}

Shot *alloc_enemy(Game *game) {
  for (int i = 0; i < kMaxEnemyShots; ++i) {
    if (!game->enemy_shots[i].alive) {
      return &game->enemy_shots[i];
    }
  }
  return nullptr;
}

void fire_enemy(Game *game, int x, int y, int w, int h, int vx, int vy) {
  Shot *shot = alloc_enemy(game);
  if (shot == nullptr) {
    return;
  }
  shot->alive = true;
  shot->x = static_cast<int16_t>((x + w / 2) * kSub);
  shot->y = static_cast<int16_t>((y + h) * kSub);
  shot->vx = static_cast<int16_t>(vx);
  shot->vy = static_cast<int16_t>(vy);
}

void start_dive(Game *game, Invader *inv) {
  int x = 0;
  int y = 0;
  inv_pixel(game, inv, &x, &y);
  inv->state = InvState::Diving;
  inv->base_x = static_cast<int16_t>(x * kSub);
  inv->x = inv->base_x;
  inv->y = static_cast<int16_t>(y * kSub);
  inv->phase = 0;
  inv->burst = 3;
}

void setup_boss(Game *game) {
  game->boss_on = true;
  game->boss_parked = false;
  game->rows = 0;
  for (int i = 0; i < kMaxInvaders; ++i) {
    game->inv[i].state = InvState::Empty;
  }
  game->boss_x = static_cast<int16_t>(((kScreenW - kBossW) / 2) * kSub);
  game->boss_y = static_cast<int16_t>((kHudH - kBossH) * kSub);
  game->boss_vx = 12;
  int hp = 8 + game->wave;
  if (game->diff == Difficulty::Easy) {
    hp = hp * 2 / 3;
  }
  if (hp < 6) {
    hp = 6;
  }
  if (game->diff == Difficulty::Hard) {
    hp += 4;
  }
  game->boss_hp = static_cast<uint8_t>(hp);
  game->boss_max = game->boss_hp;
  game->boss_cd = 24;
}

void setup_wave(Game *game) {
  game->wave_banner = kWaveWipe;
  game->clear_wait = 0;
  game->wave_awarded = false;
  game->enemy_cd = 24;
  game->dive_cd = 40;
  game->rock_cd = 70;
  game->power_on = false;
  clear_enemy_shots(game);
  if (game->wave % 5 == 0) {
    setup_boss(game);
    sound_play(Sfx::Boss);
    return;
  }
  game->boss_on = false;
  const int slot = game->wave % 5;
  game->rows = slot == 1 ? 2 : (slot == 2 ? 3 : 4);
  game->form_x = 0;
  game->form_y = static_cast<int16_t>((kHudH + 3) * kSub);
  game->form_dir = 1;
  for (int i = 0; i < kMaxInvaders; ++i) {
    Invader *inv = &game->inv[i];
    inv->col = static_cast<uint8_t>(i % kCols);
    inv->row = static_cast<uint8_t>(i / kCols);
    inv->burst = 0;
    inv->phase = 0;
    inv->state = inv->row < game->rows ? InvState::Formed : InvState::Empty;
  }
  sound_play(Sfx::Wave);
}

void begin_run(Game *game) {
  game->score = 0;
  game->lives = static_cast<uint8_t>(lives_for(game->diff));
  game->wave = 1;
  game->rank = -1;
  game->score_saved = false;
  game->spread_t = 0;
  game->shield = false;
  game->iframes = 36;
  game->fire_cd = 8;
  game->px = static_cast<int16_t>(((kScreenW - kShipW) / 2) * kSub);
  game->py = static_cast<int16_t>((kScreenH - kShipH - 2) * kSub);
  clear_combat(game);
  game->screen = Screen::Playing;
  game->nav_dir = 0;
  game->nav_hold = 0;
  setup_wave(game);
}

void insert_score(Game *game) {
  game->rank = -1;
  for (int i = 0; i < 3; ++i) {
    if (game->score > game->top[i]) {
      for (int j = 2; j > i; --j) {
        game->top[j] = game->top[j - 1];
      }
      game->top[i] = game->score;
      game->rank = static_cast<int8_t>(i);
      break;
    }
  }
  if (game->rank >= 0) {
    save_prefs(game, true);
  }
}

void hurt(Game *game) {
  if (game->iframes > 0) {
    return;
  }
  kick_shake(game, 6);
  if (game->shield) {
    game->shield = false;
    game->iframes = 20;
    clear_enemy_shots(game);
    sound_play(Sfx::Explode);
    return;
  }
  if (game->lives > 0) {
    game->lives--;
  }
  game->iframes = 48;
  game->spread_t = 0;
  clear_enemy_shots(game);
  burst_at(game, (game->px >> 4) + kShipW / 2, (game->py >> 4) + kShipH / 2, 10, 16);
  if (game->lives == 0) {
    game->screen = Screen::GameOver;
    sound_play(Sfx::Over);
    return;
  }
  sound_play(Sfx::Hurt);
}

void try_shoot(Game *game) {
  int free_slots[kMaxPlayerShots];
  int free_count = 0;
  for (int i = 0; i < kMaxPlayerShots; ++i) {
    if (!game->player_shots[i].alive) {
      free_slots[free_count++] = i;
    }
  }
  if (free_count == 0) {
    return;
  }
  int want = game->spread_t > 0 ? 3 : 1;
  if (want > free_count) {
    want = free_count;
  }
  int vxs[3] = {0, 0, 0};
  if (want == 2) {
    vxs[0] = -14;
    vxs[1] = 14;
  } else if (want == 3) {
    vxs[0] = -16;
    vxs[1] = 0;
    vxs[2] = 16;
  }
  const int origin_x = game->px + (kShipW / 2) * kSub;
  const int origin_y = game->py - 2 * kSub;
  for (int i = 0; i < want; ++i) {
    Shot *shot = &game->player_shots[free_slots[i]];
    shot->alive = true;
    shot->x = static_cast<int16_t>(origin_x);
    shot->y = static_cast<int16_t>(origin_y);
    shot->vx = static_cast<int16_t>(vxs[i]);
    shot->vy = kPlayerBulletV;
  }
  sound_play(Sfx::Shoot);
}

void move_player(Game *game, const Input *input) {
  const int vx = input->axis_x * kMaxV / kJoySpan;
  const int vy = input->axis_y * kMaxV / kJoySpan;
  game->px = static_cast<int16_t>(game->px + vx);
  game->py = static_cast<int16_t>(game->py + vy);
  const int max_x = (kScreenW - kShipW) * kSub;
  const int min_y = (kHudH + 1) * kSub;
  const int max_y = (kScreenH - kShipH - 1) * kSub;
  if (game->px < 0) {
    game->px = 0;
  } else if (game->px > max_x) {
    game->px = static_cast<int16_t>(max_x);
  }
  if (game->py < min_y) {
    game->py = static_cast<int16_t>(min_y);
  } else if (game->py > max_y) {
    game->py = static_cast<int16_t>(max_y);
  }
}

void move_shots(Game *game, Shot *shots, int count, int stamps) {
  for (int i = 0; i < count; ++i) {
    if (!shots[i].alive) {
      continue;
    }
    const int x0 = shots[i].x >> 4;
    const int y0 = shots[i].y >> 4;
    shots[i].x = static_cast<int16_t>(shots[i].x + shots[i].vx);
    shots[i].y = static_cast<int16_t>(shots[i].y + shots[i].vy);
    const int x1 = shots[i].x >> 4;
    const int y1 = shots[i].y >> 4;
    for (int s = 0; s < stamps; ++s) {
      stamp_ghost(game, x0 + (x1 - x0) * s / stamps, y0 + (y1 - y0) * s / stamps);
    }
    const int x = shots[i].x >> 4;
    const int y = shots[i].y >> 4;
    if (y < -6 || y > kScreenH || x < -8 || x > kScreenW) {
      shots[i].alive = false;
    }
  }
}

void move_bits(Game *game) {
  for (int i = 0; i < kMaxBits; ++i) {
    Bit *bit = &game->bits[i];
    if (!bit->alive) {
      continue;
    }
    stamp_ghost(game, bit->x >> 4, bit->y >> 4);
    bit->x = static_cast<int16_t>(bit->x + bit->vx);
    bit->y = static_cast<int16_t>(bit->y + bit->vy);
    if (bit->life > 0) {
      bit->life--;
    }
    const int x = bit->x >> 4;
    const int y = bit->y >> 4;
    if (bit->life == 0 || x < -4 || x > kScreenW || y < -4 || y > kScreenH) {
      bit->alive = false;
    }
  }
}

int count_state(const Game *game, InvState state) {
  int count = 0;
  for (int i = 0; i < kMaxInvaders; ++i) {
    if (game->inv[i].state == state) {
      count++;
    }
  }
  return count;
}

bool any_invader(const Game *game) {
  for (int i = 0; i < kMaxInvaders; ++i) {
    if (game->inv[i].state != InvState::Empty) {
      return true;
    }
  }
  return false;
}

void move_formation(Game *game) {
  const int formed = count_state(game, InvState::Formed);
  if (formed == 0 || game->rows == 0) {
    return;
  }
  int speed = 3 + game->wave / 2 + (game->rows * kCols - formed) / 2;
  if (game->diff == Difficulty::Easy) {
    speed -= 1;
  } else if (game->diff == Difficulty::Hard) {
    speed += 2;
  }
  if (speed < 2) {
    speed = 2;
  }
  if (speed > 16) {
    speed = 16;
  }
  game->form_x = static_cast<int16_t>(game->form_x + game->form_dir * speed);
  const int width = (kCols - 1) * kStrideX + kSaucerW;
  const int max_x = (kScreenW - width) * kSub;
  bool stepped = false;
  if (game->form_x < 0) {
    game->form_x = 0;
    game->form_dir = 1;
    stepped = true;
  } else if (game->form_x > max_x) {
    game->form_x = static_cast<int16_t>(max_x);
    game->form_dir = -1;
    stepped = true;
  }
  if (stepped) {
    game->form_y = static_cast<int16_t>(game->form_y + 4 * kSub);
  }
  const int bottom = (game->form_y >> 4) + (game->rows - 1) * kStrideY + kSaucerH;
  if (bottom >= 110) {
    for (int i = 0; i < kMaxInvaders; ++i) {
      if (game->inv[i].state == InvState::Formed) {
        start_dive(game, &game->inv[i]);
      }
    }
  }
}

void move_divers(Game *game) {
  const int sway_scale = game->diff == Difficulty::Hard ? 8 : 5;
  const int vy = dive_vy(game->diff);
  for (int i = 0; i < kMaxInvaders; ++i) {
    Invader *inv = &game->inv[i];
    if (inv->state == InvState::Diving) {
      inv->phase++;
      const int sway = kSin[(inv->phase / 2) & 31] * sway_scale;
      inv->x = static_cast<int16_t>(inv->base_x + sway);
      inv->y = static_cast<int16_t>(inv->y + vy);
      const int min_x = 0;
      const int max_x = (kScreenW - kSaucerW) * kSub;
      if (inv->x < min_x) {
        inv->x = min_x;
      } else if (inv->x > max_x) {
        inv->x = static_cast<int16_t>(max_x);
      }
      if (inv->burst > 0 && (inv->phase % 14) == 1) {
        int x = 0;
        int y = 0;
        inv_pixel(game, inv, &x, &y);
        fire_enemy(game, x, y, kSaucerW, kSaucerH, 0, 22);
        inv->burst--;
      }
      if (inv->y > 120 * kSub) {
        inv->state = InvState::Returning;
      }
    } else if (inv->state == InvState::Returning) {
      const int tx = ((game->form_x >> 4) + inv->col * kStrideX) * kSub;
      const int ty = ((game->form_y >> 4) + inv->row * kStrideY) * kSub;
      if (inv->x < tx) {
        inv->x = static_cast<int16_t>(inv->x + 24);
      } else if (inv->x > tx) {
        inv->x = static_cast<int16_t>(inv->x - 24);
      }
      if (inv->y < ty) {
        inv->y = static_cast<int16_t>(inv->y + 24);
      } else if (inv->y > ty) {
        inv->y = static_cast<int16_t>(inv->y - 24);
      }
      if (iabs(inv->x - tx) <= 24 && iabs(inv->y - ty) <= 24) {
        inv->state = InvState::Formed;
      }
    }
  }
}

void maybe_dive(Game *game) {
  if (game->dive_cd > 0) {
    game->dive_cd--;
    return;
  }
  const int diving = count_state(game, InvState::Diving) + count_state(game, InvState::Returning);
  const int cap = game->diff == Difficulty::Easy ? 1 : 2;
  if (diving >= cap || count_state(game, InvState::Formed) < 2) {
    game->dive_cd = static_cast<uint8_t>(dive_period(game->diff));
    return;
  }
  int picks[kCols];
  int pick_count = 0;
  for (int col = 0; col < kCols; ++col) {
    int best = -1;
    for (int i = 0; i < kMaxInvaders; ++i) {
      if (game->inv[i].state == InvState::Formed && game->inv[i].col == col) {
        if (best < 0 || game->inv[i].row > game->inv[best].row) {
          best = i;
        }
      }
    }
    if (best >= 0) {
      picks[pick_count++] = best;
    }
  }
  if (pick_count > 0) {
    start_dive(game, &game->inv[picks[rand_n(pick_count)]]);
  }
  game->dive_cd = static_cast<uint8_t>(dive_period(game->diff));
}

void maybe_enemy_fire(Game *game) {
  if (game->enemy_cd > 0) {
    game->enemy_cd--;
    return;
  }
  int picks[kCols];
  int pick_count = 0;
  for (int col = 0; col < kCols; ++col) {
    int best = -1;
    for (int i = 0; i < kMaxInvaders; ++i) {
      if (game->inv[i].state == InvState::Formed && game->inv[i].col == col) {
        if (best < 0 || game->inv[i].row > game->inv[best].row) {
          best = i;
        }
      }
    }
    if (best >= 0) {
      picks[pick_count++] = best;
    }
  }
  if (pick_count > 0) {
    Invader *inv = &game->inv[picks[rand_n(pick_count)]];
    int x = 0;
    int y = 0;
    inv_pixel(game, inv, &x, &y);
    fire_enemy(game, x, y, kSaucerW, kSaucerH, 0, game->diff == Difficulty::Hard ? 28 : 20);
  }
  int period = fire_period(game->diff) - game->wave * 2;
  if (period < 14) {
    period = 14;
  }
  game->enemy_cd = static_cast<uint8_t>(period);
}

void maybe_rock(Game *game) {
  if (game->rock_cd > 0) {
    game->rock_cd--;
    return;
  }
  Rock *rock = nullptr;
  for (int i = 0; i < kMaxRocks; ++i) {
    if (!game->rocks[i].alive) {
      rock = &game->rocks[i];
      break;
    }
  }
  if (rock != nullptr) {
    rock->alive = true;
    rock->x = static_cast<int16_t>(rand_n(kScreenW - kRockW) * kSub);
    rock->y = static_cast<int16_t>((kHudH + 1) * kSub);
    rock->vy = static_cast<int16_t>(6 + rand_n(7));
    rock->vx = static_cast<int16_t>(rand_n(5) - 2);
  }
  int period = rock_period(game->diff) - game->wave * 3;
  if (period < 36) {
    period = 36;
  }
  game->rock_cd = static_cast<uint8_t>(period);
}

void move_rocks(Game *game) {
  for (int i = 0; i < kMaxRocks; ++i) {
    Rock *rock = &game->rocks[i];
    if (!rock->alive) {
      continue;
    }
    rock->x = static_cast<int16_t>(rock->x + rock->vx);
    rock->y = static_cast<int16_t>(rock->y + rock->vy);
    const int x = rock->x >> 4;
    const int y = rock->y >> 4;
    if (x < 0) {
      rock->x = 0;
      rock->vx = static_cast<int16_t>(iabs(rock->vx));
    } else if (x > kScreenW - kRockW) {
      rock->x = static_cast<int16_t>((kScreenW - kRockW) * kSub);
      rock->vx = static_cast<int16_t>(-iabs(rock->vx));
    }
    if (y > kScreenH) {
      rock->alive = false;
    }
  }
}

void move_boss(Game *game) {
  if (!game->boss_on) {
    return;
  }
  if (!game->boss_parked) {
    game->boss_y = static_cast<int16_t>(game->boss_y + 10);
    if (game->boss_y >= 16 * kSub) {
      game->boss_y = static_cast<int16_t>(16 * kSub);
      game->boss_parked = true;
    }
    return;
  }
  game->boss_x = static_cast<int16_t>(game->boss_x + game->boss_vx);
  const int max_x = (kScreenW - kBossW) * kSub;
  if (game->boss_x < 0) {
    game->boss_x = 0;
    game->boss_vx = static_cast<int16_t>(iabs(game->boss_vx));
  } else if (game->boss_x > max_x) {
    game->boss_x = static_cast<int16_t>(max_x);
    game->boss_vx = static_cast<int16_t>(-iabs(game->boss_vx));
  }
  if (game->wave_banner > 0) {
    return;
  }
  if (game->boss_cd > 0) {
    game->boss_cd--;
    return;
  }
  const int x = game->boss_x >> 4;
  const int y = game->boss_y >> 4;
  fire_enemy(game, x, y, kBossW, kBossH, -14, 16);
  fire_enemy(game, x, y, kBossW, kBossH, 0, 22);
  fire_enemy(game, x, y, kBossW, kBossH, 14, 16);
  int period = game->diff == Difficulty::Easy ? 42 : (game->diff == Difficulty::Hard ? 20 : 30);
  game->boss_cd = static_cast<uint8_t>(period);
}

void kill_invader(Game *game, Invader *inv) {
  int x = 0;
  int y = 0;
  inv_pixel(game, inv, &x, &y);
  int points = 10 * (game->rows - inv->row);
  if (points < 10) {
    points = 10;
  }
  if (inv->state != InvState::Formed) {
    points += 20;
  }
  inv->state = InvState::Empty;
  add_score(game, points);
  burst_at(game, x + kSaucerW / 2, y + kSaucerH / 2, 8, 14);
  sound_play(Sfx::Explode);
  if (game->power_on) {
    return;
  }
  int chance = game->diff == Difficulty::Easy ? 18 : (game->diff == Difficulty::Hard ? 8 : 12);
  if (rand_n(100) < chance) {
    game->power_on = true;
    game->power_kind = static_cast<uint8_t>(rand_n(2));
    game->power_x = static_cast<int16_t>(x * kSub);
    game->power_y = static_cast<int16_t>(y * kSub);
  }
}

void move_power(Game *game) {
  if (!game->power_on) {
    return;
  }
  game->power_y = static_cast<int16_t>(game->power_y + 8);
  if ((game->power_y >> 4) > kScreenH) {
    game->power_on = false;
  }
}

void collide(Game *game) {
  const int ship_x = (game->px >> 4) + 3;
  const int ship_y = (game->py >> 4) + 2;
  const int ship_w = kShipW - 6;
  const int ship_h = kShipH - 4;

  for (int s = 0; s < kMaxPlayerShots; ++s) {
    Shot *shot = &game->player_shots[s];
    if (!shot->alive) {
      continue;
    }
    const int bx = shot->x >> 4;
    const int by = shot->y >> 4;
    bool spent = false;
    for (int i = 0; i < kMaxInvaders && !spent; ++i) {
      if (game->inv[i].state == InvState::Empty) {
        continue;
      }
      int x = 0;
      int y = 0;
      inv_pixel(game, &game->inv[i], &x, &y);
      if (overlap(bx, by, 1, 4, x, y, kSaucerW, kSaucerH)) {
        kill_invader(game, &game->inv[i]);
        spent = true;
      }
    }
    for (int i = 0; i < kMaxRocks && !spent; ++i) {
      if (!game->rocks[i].alive) {
        continue;
      }
      const int x = game->rocks[i].x >> 4;
      const int y = game->rocks[i].y >> 4;
      if (overlap(bx, by, 1, 4, x, y, kRockW, kRockH)) {
        game->rocks[i].alive = false;
        add_score(game, 15);
        burst_at(game, x + kRockW / 2, y + kRockH / 2, 10, 16);
        sound_play(Sfx::Explode);
        spent = true;
      }
    }
    if (!spent && game->boss_on) {
      const int x = game->boss_x >> 4;
      const int y = game->boss_y >> 4;
      if (overlap(bx, by, 1, 4, x + 1, y + 1, kBossW - 2, kBossH - 2)) {
        spent = true;
        if (game->boss_hp > 0) {
          game->boss_hp--;
        }
        burst_at(game, bx, by, 4, 8);
        if (game->boss_hp == 0) {
          game->boss_on = false;
          add_score(game, 250 + game->wave * 10);
          game->flash = 3;
          kick_shake(game, 8);
          burst_at(game, x + kBossW / 2, y + kBossH / 2, 16, 18);
          sound_play(Sfx::Explode);
        }
      }
    }
    if (spent) {
      shot->alive = false;
    }
  }

  if (game->power_on) {
    const int x = game->power_x >> 4;
    const int y = game->power_y >> 4;
    if (overlap(ship_x, ship_y, ship_w, ship_h, x, y, kPowerW, kPowerH)) {
      if (game->power_kind == 0) {
        game->spread_t = 240;
      } else {
        game->shield = true;
      }
      game->power_on = false;
      sound_play(Sfx::Power);
    }
  }

  for (int i = 0; i < kMaxEnemyShots; ++i) {
    if (!game->enemy_shots[i].alive) {
      continue;
    }
    const int x = game->enemy_shots[i].x >> 4;
    const int y = game->enemy_shots[i].y >> 4;
    if (overlap(ship_x, ship_y, ship_w, ship_h, x, y, 2, 3)) {
      game->enemy_shots[i].alive = false;
      hurt(game);
      if (game->screen != Screen::Playing) {
        return;
      }
    }
  }

  for (int i = 0; i < kMaxRocks; ++i) {
    if (!game->rocks[i].alive) {
      continue;
    }
    const int x = game->rocks[i].x >> 4;
    const int y = game->rocks[i].y >> 4;
    if (overlap(ship_x, ship_y, ship_w, ship_h, x, y, kRockW, kRockH)) {
      game->rocks[i].alive = false;
      burst_at(game, x + kRockW / 2, y + kRockH / 2, 8, 12);
      hurt(game);
      if (game->screen != Screen::Playing) {
        return;
      }
    }
  }

  if (game->boss_on) {
    const int x = game->boss_x >> 4;
    const int y = game->boss_y >> 4;
    if (overlap(ship_x, ship_y, ship_w, ship_h, x, y, kBossW, kBossH)) {
      hurt(game);
      if (game->screen != Screen::Playing) {
        return;
      }
    }
  }

  for (int i = 0; i < kMaxInvaders; ++i) {
    if (game->inv[i].state == InvState::Empty) {
      continue;
    }
    int x = 0;
    int y = 0;
    inv_pixel(game, &game->inv[i], &x, &y);
    if (overlap(ship_x, ship_y, ship_w, ship_h, x, y, kSaucerW, kSaucerH)) {
      hurt(game);
      return;
    }
  }
}

void check_wave(Game *game) {
  if (game->wave_banner > 0) {
    return;
  }
  if (any_invader(game) || game->boss_on) {
    game->wave_awarded = false;
    game->clear_wait = 0;
    return;
  }
  if (!game->wave_awarded) {
    add_score(game, 50 * game->wave);
    game->wave_awarded = true;
    game->clear_wait = 28;
    return;
  }
  if (game->clear_wait > 0) {
    game->clear_wait--;
    if (game->clear_wait == 0) {
      if (game->wave < 99) {
        game->wave++;
      }
      setup_wave(game);
    }
  }
}

void step_play(Game *game, const Input *input) {
  if (input->click_edge) {
    game->screen = Screen::Paused;
    game->pause_i = 0;
    game->nav_dir = 0;
    game->nav_hold = 0;
    sound_play(Sfx::Click);
    return;
  }
  move_player(game, input);
  if (game->iframes > 0) {
    game->iframes--;
  }
  if (game->spread_t > 0) {
    game->spread_t--;
  }
  if (game->fire_cd > 0) {
    game->fire_cd--;
  } else if (input->fire_held) {
    try_shoot(game);
    game->fire_cd = 5;
  }
  move_shots(game, game->player_shots, kMaxPlayerShots, 3);
  move_shots(game, game->enemy_shots, kMaxEnemyShots, 2);
  move_bits(game);
  move_power(game);
  move_rocks(game);
  if (game->wave_banner > 0) {
    game->wave_banner--;
  } else {
    move_formation(game);
    move_divers(game);
    maybe_dive(game);
    maybe_enemy_fire(game);
    maybe_rock(game);
  }
  move_boss(game);
  collide(game);
  if (game->screen == Screen::Playing) {
    check_wave(game);
  }
}

bool nav_edge(Game *game, const Input *input, int dir_out[1]) {
  int dir = 0;
  if (input->axis_y < -400) {
    dir = -1;
  } else if (input->axis_y > 400) {
    dir = 1;
  }
  if (dir == 0) {
    game->nav_dir = 0;
    game->nav_hold = 0;
    return false;
  }
  if (dir != game->nav_dir) {
    game->nav_dir = static_cast<int8_t>(dir);
    game->nav_hold = 0;
    dir_out[0] = dir;
    return true;
  }
  game->nav_hold++;
  if (game->nav_hold == 8 || (game->nav_hold > 8 && ((game->nav_hold - 8) % 5) == 0)) {
    dir_out[0] = dir;
    return true;
  }
  return false;
}

void move_index(uint8_t *index, int dir, int count) {
  int next = static_cast<int>(*index) + dir;
  if (next < 0) {
    next = 0;
  } else if (next >= count) {
    next = count - 1;
  }
  *index = static_cast<uint8_t>(next);
}

const char *diff_name(Difficulty diff) {
  if (diff == Difficulty::Easy) {
    return "EASY";
  }
  if (diff == Difficulty::Hard) {
    return "HARD";
  }
  return "NORMAL";
}

void step_menu(Game *game, const Input *input) {
  int dir = 0;
  const int before = game->menu_i;
  if (nav_edge(game, input, &dir)) {
    move_index(&game->menu_i, dir, 4);
    if (game->menu_i != before) {
      sound_play(Sfx::Click);
    }
  }
  if (!input->fire_edge) {
    return;
  }
  if (game->menu_i == 0) {
    sound_play(Sfx::Confirm);
    begin_run(game);
    return;
  }
  if (game->menu_i == 1) {
    uint8_t next = static_cast<uint8_t>(game->diff) + 1;
    if (next > static_cast<uint8_t>(Difficulty::Hard)) {
      next = 0;
    }
    game->diff = static_cast<Difficulty>(next);
    save_prefs(game, false);
    sound_play(Sfx::Confirm);
    Serial.printf("Difficulty %s\n", diff_name(game->diff));
    return;
  }
  if (game->menu_i == 2) {
    game->screen = Screen::Scores;
    game->nav_dir = 0;
    sound_play(Sfx::Confirm);
    return;
  }
  if (game->sound_on) {
    sound_play(Sfx::Confirm);
    game->sound_on = false;
    sound_set_enabled(false);
  } else {
    game->sound_on = true;
    sound_set_enabled(true);
    sound_play(Sfx::Confirm);
  }
  save_prefs(game, false);
}

void step_scores(Game *game, const Input *input) {
  if (input->click_edge || input->fire_edge) {
    game->screen = Screen::Menu;
    game->nav_dir = 0;
    sound_play(Sfx::Click);
  }
}

void step_pause(Game *game, const Input *input) {
  int dir = 0;
  const int before = game->pause_i;
  if (nav_edge(game, input, &dir)) {
    move_index(&game->pause_i, dir, 3);
    if (game->pause_i != before) {
      sound_play(Sfx::Click);
    }
  }
  if (input->click_edge) {
    game->screen = Screen::Playing;
    sound_play(Sfx::Confirm);
    return;
  }
  if (!input->fire_edge) {
    return;
  }
  if (game->pause_i == 0) {
    game->screen = Screen::Playing;
    sound_play(Sfx::Confirm);
    return;
  }
  if (game->pause_i == 1) {
    begin_run(game);
    return;
  }
  game->screen = Screen::Menu;
  game->nav_dir = 0;
  sound_play(Sfx::Click);
}

void step_over(Game *game, const Input *input) {
  if (!game->score_saved) {
    insert_score(game);
    game->score_saved = true;
    Serial.printf("Game over score %lu\n", static_cast<unsigned long>(game->score));
  }
  if (input->fire_edge) {
    begin_run(game);
    return;
  }
  if (input->click_edge) {
    game->screen = Screen::Menu;
    game->nav_dir = 0;
    sound_play(Sfx::Click);
  }
}

}  // namespace

void game_init(Game *game) {
  *game = Game{};
  game->screen = Screen::Menu;
  game->diff = Difficulty::Normal;
  game->sound_on = true;
  game->rank = -1;
  load_prefs(game);
  seed_stars(game);
  sound_set_enabled(game->sound_on);
  const char *diff = "NORMAL";
  if (game->diff == Difficulty::Easy) {
    diff = "EASY";
  } else if (game->diff == Difficulty::Hard) {
    diff = "HARD";
  }
  Serial.printf("Scores %lu %lu %lu, sound %s, %s\n", static_cast<unsigned long>(game->top[0]),
                static_cast<unsigned long>(game->top[1]), static_cast<unsigned long>(game->top[2]),
                game->sound_on ? "on" : "off", diff);
}

void game_step(Game *game, const Input *input) {
  game->frame++;
  update_feel(game, input);
  if (game->screen != Screen::Paused) {
    move_stars(game);
    tick_juice(game);
  }
  switch (game->screen) {
    case Screen::Menu:
      step_menu(game, input);
      break;
    case Screen::Scores:
      step_scores(game, input);
      break;
    case Screen::Playing:
      step_play(game, input);
      break;
    case Screen::Paused:
      step_pause(game, input);
      break;
    case Screen::GameOver:
      step_over(game, input);
      break;
  }
}
