#include "game.h"

#include "art.h"
#include "oled.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kMapW = 16;
constexpr int kMapH = 16;
constexpr int kViewW = 128;
constexpr int kViewH = 56;
constexpr int kHorizon = 22;
constexpr float kPlane = 0.80f;
constexpr float kTau = 6.2831853f;
constexpr int kMaxEnemy = 8;
constexpr int kMaxDoor = 8;
constexpr int kMaxPickup = 12;
constexpr int kMaxShot = 6;

// +Y is south, so the rows below read the way they are drawn. Angle 0 faces east.
// The red door is the only way into the exit row.
const char *const kMap[kMapH] = {
    "################", "#@..a..........#", "####d###########", "#..............#",
    "#..e....####...#", "#.......#k#....#", "#..####.#.#..h.#", "#.a#....#.#....#",
    "#..#.m..d.d....#", "#..#....#.#..g.#", "#..######.#....#", "#e........#..v.#",
    "######r#########", "#..............#", "#.....x........#", "################",
};

enum Kind : uint8_t { kGuard, kBeast };

struct Enemy {
  float x;
  float y;
  float lx;
  float ly;
  int hp;
  uint32_t next_act;
  uint32_t saw_until;
  uint32_t flash_until;
  Kind kind;
  bool alive;
};

struct Door {
  uint8_t x;
  uint8_t y;
  uint8_t open;
  int8_t vel;
  bool red;
  uint32_t hold_until;
};

struct Pickup {
  float x;
  float y;
  char kind;
  bool live;
};

struct Shot {
  float x;
  float y;
  float vx;
  float vy;
  uint8_t owner;
  bool live;
};

struct Bill {
  float depth;
  float x;
  float y;
  const char *const *rows;
  uint8_t w;
  uint8_t h;
  uint8_t scale_pct;
  bool flash;
};

enum class Mode : uint8_t { Title, Play, Dead, Won };

Mode g_mode = Mode::Title;
bool g_map = false;
float g_px = 1.5f;
float g_py = 1.5f;
float g_ang = 0;
float g_dir_x = 1;
float g_dir_y = 0;
float g_plane_x = 0;
float g_plane_y = kPlane;
int g_bob = 0;
float g_bob_phase = 0;
int g_kick = 0;
int g_hp = 100;
int g_armor = 0;
int g_ammo = 24;
bool g_key = false;
bool g_shotgun = false;
int g_exit_x = -1;
int g_exit_y = -1;
uint32_t g_next_shot = 0;
uint32_t g_flash_until = 0;
uint32_t g_pain_until = 0;
uint32_t g_invuln = 0;
uint32_t g_banner_until = 0;
char g_banner[16] = "";

Enemy g_enemies[kMaxEnemy];
int g_enemy_n = 0;
Door g_doors[kMaxDoor];
int g_door_n = 0;
uint8_t g_door_at[kMapH][kMapW];
Pickup g_pickups[kMaxPickup];
int g_pickup_n = 0;
Shot g_shots[kMaxShot];
uint16_t g_z[kViewW];
Bill g_bills[24];
int g_bill_n = 0;

void say(const char *text, uint32_t ms) {
  strncpy(g_banner, text, sizeof g_banner - 1);
  g_banner[sizeof g_banner - 1] = '\0';
  g_banner_until = millis() + ms;
}

void aim() {
  g_dir_x = cosf(g_ang);
  g_dir_y = sinf(g_ang);
  // Screen-right is clockwise on this y-down map.
  g_plane_x = -sinf(g_ang) * kPlane;
  g_plane_y = cosf(g_ang) * kPlane;
}

char raw_at(int x, int y) {
  if (x < 0 || y < 0 || x >= kMapW || y >= kMapH) {
    return '#';
  }
  return kMap[y][x];
}

bool block_ray(int x, int y) {
  const char c = raw_at(x, y);
  if (c == '#') {
    return true;
  }
  if (c == 'd' || c == 'r') {
    const uint8_t id = g_door_at[y][x];
    if (id == 0xFF) {
      return true;
    }
    return g_doors[id].open < 230;
  }
  return false;
}

bool block_body(float x, float y) {
  const float r = 0.18f;
  const float px[4] = {x - r, x + r, x - r, x + r};
  const float py[4] = {y - r, y - r, y + r, y + r};
  for (int i = 0; i < 4; ++i) {
    const int cx = (int)floorf(px[i]);
    const int cy = (int)floorf(py[i]);
    const char c = raw_at(cx, cy);
    if (c == '#') {
      return true;
    }
    if (c == 'd' || c == 'r') {
      const uint8_t id = g_door_at[cy][cx];
      if (id == 0xFF || g_doors[id].open < 120) {
        return true;
      }
    }
  }
  return false;
}

bool los(float x0, float y0, float x1, float y1) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float dist = sqrtf(dx * dx + dy * dy);
  const int steps = (int)(dist / 0.15f);
  for (int i = 1; i < steps; ++i) {
    const float x = x0 + dx * (float)i / (float)steps;
    const float y = y0 + dy * (float)i / (float)steps;
    if (block_ray((int)floorf(x), (int)floorf(y))) {
      return false;
    }
  }
  return true;
}

bool art_width(const char *const *rows, int n, int w) {
  for (int i = 0; i < n; ++i) {
    if ((int)strlen(rows[i]) != w) {
      Serial.printf("art width %d at row %d\n", (int)strlen(rows[i]), i);
      return false;
    }
  }
  return true;
}

void check_art() {
  const bool ok = art_width(kTexBrick, 16, 16) && art_width(kTexTech, 16, 16) &&
                  art_width(kTexRib, 16, 16) && art_width(kTexSupport, 16, 16) &&
                  art_width(kTexDoor, 16, 16) && art_width(kTexRed, 16, 16) &&
                  art_width(kSprGuard, 16, 16) && art_width(kSprBeast, 16, 16) &&
                  art_width(kSprCorpse, 16, 16) && art_width(kSprPistol, 16, 16) &&
                  art_width(kSprShotgun, 16, 16) && art_width(kSprKey, 8, 8) &&
                  art_width(kSprAmmo, 8, 8) && art_width(kSprMed, 8, 8) &&
                  art_width(kSprArmor, 8, 8) && art_width(kSprGun, 8, 8) &&
                  art_width(kSprExit, 8, 8) && art_width(kSprBolt, 5, 5);
  if (!ok) {
    Serial.println("sprite rows are the wrong width");
  }
}

void begin_level() {
  g_enemy_n = 0;
  g_door_n = 0;
  g_pickup_n = 0;
  g_exit_x = -1;
  g_exit_y = -1;
  memset(g_door_at, 0xFF, sizeof g_door_at);
  memset(g_shots, 0, sizeof g_shots);
  g_hp = 100;
  g_armor = 0;
  g_ammo = 24;
  g_key = false;
  g_shotgun = false;
  g_ang = 0;
  g_bob = 0;
  g_bob_phase = 0;
  g_kick = 0;
  g_map = false;
  g_banner[0] = '\0';
  g_banner_until = 0;
  g_pain_until = 0;
  g_invuln = 0;
  g_flash_until = 0;
  g_next_shot = millis() + 400;
  g_px = 1.5f;
  g_py = 1.5f;

  for (int y = 0; y < kMapH; ++y) {
    for (int x = 0; x < kMapW; ++x) {
      const char c = kMap[y][x];
      const float cx = (float)x + 0.5f;
      const float cy = (float)y + 0.5f;
      if (c == '@') {
        g_px = cx;
        g_py = cy;
      } else if ((c == 'e' || c == 'm') && g_enemy_n < kMaxEnemy) {
        Enemy &e = g_enemies[g_enemy_n];
        e = {};
        e.x = cx;
        e.y = cy;
        e.lx = cx;
        e.ly = cy;
        e.kind = (c == 'm') ? kBeast : kGuard;
        e.hp = (e.kind == kBeast) ? 64 : 30;
        e.alive = true;
        e.next_act = millis() + (uint32_t)g_enemy_n * 350u;
        ++g_enemy_n;
      } else if ((c == 'd' || c == 'r') && g_door_n < kMaxDoor) {
        Door &d = g_doors[g_door_n];
        d = {};
        d.x = (uint8_t)x;
        d.y = (uint8_t)y;
        d.red = (c == 'r');
        g_door_at[y][x] = (uint8_t)g_door_n;
        ++g_door_n;
      } else if ((c == 'a' || c == 'h' || c == 'v' || c == 'g' || c == 'k') &&
                 g_pickup_n < kMaxPickup) {
        Pickup &p = g_pickups[g_pickup_n];
        p.x = cx;
        p.y = cy;
        p.kind = c;
        p.live = true;
        ++g_pickup_n;
      } else if (c == 'x') {
        g_exit_x = x;
        g_exit_y = y;
      }
    }
  }

  g_mode = Mode::Play;
  aim();
  Serial.printf("entry: %d enemies, %d doors\n", g_enemy_n, g_door_n);
}

void pixel(uint8_t *buf, int x, int y, bool on) {
  if ((unsigned)x >= 128u || (unsigned)y >= 64u) {
    return;
  }
  uint8_t &cell = buf[x + (y >> 3) * 128];
  const uint8_t bit = (uint8_t)(1u << (y & 7));
  if (on) {
    cell |= bit;
  } else {
    cell &= (uint8_t)~bit;
  }
}

void text_center(int y, const char *s) {
  const int w = oled.getStrWidth(s);
  int x = (128 - w) / 2;
  if (x < 0) {
    x = 0;
  }
  oled.drawStr(x, y, s);
}

void draw_title() {
  oled.clearBuffer();
  text_center(2, "MINI DOOM");
  text_center(14, "entry");
  text_center(26, "stick moves");
  text_center(35, "touch fires");
  text_center(44, "click door/map");
  text_center(54, "TOUCH START");
  oled.sendBuffer();
}

void fill_box(uint8_t *buf, int x0, int y0, int w, int h) {
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      pixel(buf, x0 + x, y0 + y, true);
    }
  }
}

void draw_map() {
  oled.clearBuffer();
  uint8_t *buf = oled.getBufferPtr();
  constexpr int kCell = 4;
  const int ox = (128 - kMapW * kCell) / 2;
  const int oy = (64 - kMapH * kCell) / 2;
  for (int y = 0; y < kMapH; ++y) {
    for (int x = 0; x < kMapW; ++x) {
      const char c = kMap[y][x];
      const int x0 = ox + x * kCell;
      const int y0 = oy + y * kCell;
      if (c == '#') {
        fill_box(buf, x0, y0, 3, 3);
      } else if (c == 'd' || c == 'r') {
        const uint8_t id = g_door_at[y][x];
        if (id != 0xFF && g_doors[id].open < 140) {
          fill_box(buf, x0, y0 + 1, 3, 2);
        }
      }
    }
  }
  for (int i = 0; i < g_pickup_n; ++i) {
    if (!g_pickups[i].live) {
      continue;
    }
    const int x = ox + (int)(g_pickups[i].x * kCell);
    const int y = oy + (int)(g_pickups[i].y * kCell);
    pixel(buf, x, y, true);
    if (g_pickups[i].kind == 'k') {
      pixel(buf, x + 1, y, true);
    }
  }
  if (g_exit_x >= 0 && ((millis() / 200) & 1u) == 0) {
    pixel(buf, ox + g_exit_x * kCell + 1, oy + g_exit_y * kCell + 1, true);
  }
  for (int i = 0; i < g_enemy_n; ++i) {
    if (!g_enemies[i].alive) {
      continue;
    }
    const int x = ox + (int)(g_enemies[i].x * kCell);
    const int y = oy + (int)(g_enemies[i].y * kCell);
    if (g_enemies[i].kind == kBeast) {
      fill_box(buf, x, y, 2, 2);
    } else {
      pixel(buf, x, y, true);
    }
  }
  const int px = ox + (int)(g_px * kCell);
  const int py = oy + (int)(g_py * kCell);
  pixel(buf, px, py, true);
  const int fx = (int)lroundf(cosf(g_ang));
  const int fy = (int)lroundf(sinf(g_ang));
  pixel(buf, px + fx, py + fy, true);
  oled.drawStr(1, 2, "MAP");
  oled.drawStr(1, 54, "click");
  oled.sendBuffer();
}

void dither_column(uint8_t *buf, int x, int y0, int y1) {
  if (y0 < 0) {
    y0 = 0;
  }
  if (y1 > kViewH) {
    y1 = kViewH;
  }
  const int horizon = kHorizon + g_bob;
  for (int y = y0; y < y1; ++y) {
    int band = y - horizon;
    if (band < 0) {
      band = -band;
    }
    int step = 5;
    if (band > 18) {
      step = 2;
    } else if (band > 9) {
      step = 3;
    }
    if (((x * 3 + y) % step) == 0) {
      pixel(buf, x, y, true);
    }
  }
}

void paint_tex(uint8_t *buf, int x, int y0, int y1, int span0, int span_h,
               const char *const *tex, int tex_x, int shade) {
  if (span_h < 1) {
    return;
  }
  if (y0 < 0) {
    y0 = 0;
  }
  if (y1 > kViewH) {
    y1 = kViewH;
  }
  if (tex_x < 0) {
    tex_x = 0;
  }
  if (tex_x > 15) {
    tex_x = 15;
  }
  for (int y = y0; y < y1; ++y) {
    int ty = (y - span0) * 16 / span_h;
    if (ty < 0) {
      ty = 0;
    }
    if (ty > 15) {
      ty = 15;
    }
    if (tex[ty][tex_x] != '#') {
      continue;
    }
    if (shade == 1 && ((x ^ y) & 1)) {
      continue;
    }
    if (shade >= 2 && ((x + y) & 3)) {
      continue;
    }
    pixel(buf, x, y, true);
  }
}

bool project(float x, float y, float *tx, float *ty) {
  const float dx = x - g_px;
  const float dy = y - g_py;
  const float det = g_plane_x * g_dir_y - g_dir_x * g_plane_y;
  if (det > -0.001f && det < 0.001f) {
    return false;
  }
  const float inv = 1.0f / det;
  *tx = inv * (g_dir_y * dx - g_dir_x * dy);
  *ty = inv * (-g_plane_y * dx + g_plane_x * dy);
  return *ty > 0.08f;
}

void add_bill(float x, float y, const char *const *rows, int w, int h, int scale, bool flash) {
  if (g_bill_n >= 24) {
    return;
  }
  float tx = 0;
  float ty = 0;
  if (!project(x, y, &tx, &ty)) {
    return;
  }
  Bill &b = g_bills[g_bill_n];
  b.depth = ty;
  b.x = x;
  b.y = y;
  b.rows = rows;
  b.w = (uint8_t)w;
  b.h = (uint8_t)h;
  b.scale_pct = (uint8_t)scale;
  b.flash = flash;
  ++g_bill_n;
}

void draw_bill(uint8_t *buf, const Bill &b) {
  float tx = 0;
  float ty = 0;
  if (!project(b.x, b.y, &tx, &ty)) {
    return;
  }
  const int column_h = (int)(kViewH / ty);
  if (column_h < 1) {
    return;
  }
  int sprite_h = column_h * b.scale_pct / 100;
  if (sprite_h < 2) {
    return;
  }
  int sprite_w = sprite_h * b.w / b.h;
  if (sprite_w < 1) {
    sprite_w = 1;
  }
  const int horizon = kHorizon + g_bob;
  const int bottom = horizon + column_h / 2;
  const int top = bottom - sprite_h;
  const int left = (int)(kViewW * 0.5f * (1.0f + tx / ty)) - sprite_w / 2;
  const uint16_t sz = (uint16_t)fminf(ty * 256.0f, 65535.0f);
  for (int sx = 0; sx < sprite_w; ++sx) {
    const int x = left + sx;
    if (x < 0 || x >= kViewW || sz >= g_z[x]) {
      continue;
    }
    int tex_x = sx * b.w / sprite_w;
    if (tex_x >= b.w) {
      tex_x = b.w - 1;
    }
    for (int sy = 0; sy < sprite_h; ++sy) {
      const int y = top + sy;
      if (y < 0 || y >= kViewH) {
        continue;
      }
      if (b.flash) {
        pixel(buf, x, y, true);
        continue;
      }
      int tex_y = sy * b.h / sprite_h;
      if (tex_y >= b.h) {
        tex_y = b.h - 1;
      }
      if (b.rows[tex_y][tex_x] == '#') {
        pixel(buf, x, y, true);
      }
    }
  }
}

void stamp(uint8_t *buf, const char *const *rows, int w, int h, int ox, int oy) {
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (rows[y][x] != '#') {
        continue;
      }
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          pixel(buf, ox + x + dx, oy + y + dy, false);
        }
      }
    }
  }
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (rows[y][x] == '#') {
        pixel(buf, ox + x, oy + y, true);
      }
    }
  }
}

void draw_crosshair(uint8_t *buf) {
  const int x = kViewW / 2;
  const int y = kHorizon + g_bob;
  for (int i = -3; i <= 3; ++i) {
    if (i == 0) {
      continue;
    }
    pixel(buf, x + i, y, false);
    pixel(buf, x, y + i, false);
  }
  for (int i = -2; i <= 2; ++i) {
    if (i == 0) {
      continue;
    }
    pixel(buf, x + i, y, true);
    pixel(buf, x, y + i, true);
  }
}

void draw_world() {
  aim();
  oled.clearBuffer();
  uint8_t *buf = oled.getBufferPtr();
  for (int i = 0; i < kViewW; ++i) {
    g_z[i] = 65535;
  }

  for (int col = 0; col < kViewW; ++col) {
    const float camera_x = 2.0f * (float)col / (float)kViewW - 1.0f;
    const float ray_x = g_dir_x + g_plane_x * camera_x;
    const float ray_y = g_dir_y + g_plane_y * camera_x;
    int map_x = (int)floorf(g_px);
    int map_y = (int)floorf(g_py);
    const float delta_x = (ray_x == 0.0f) ? 1.0e30f : fabsf(1.0f / ray_x);
    const float delta_y = (ray_y == 0.0f) ? 1.0e30f : fabsf(1.0f / ray_y);
    int step_x = 1;
    int step_y = 1;
    float side_x = 0;
    float side_y = 0;
    if (ray_x < 0.0f) {
      step_x = -1;
      side_x = (g_px - (float)map_x) * delta_x;
    } else {
      side_x = ((float)map_x + 1.0f - g_px) * delta_x;
    }
    if (ray_y < 0.0f) {
      step_y = -1;
      side_y = (g_py - (float)map_y) * delta_y;
    } else {
      side_y = ((float)map_y + 1.0f - g_py) * delta_y;
    }

    int side = 0;
    int hit = 0;
    int door_i = -1;
    for (int n = 0; n < 48; ++n) {
      if (side_x < side_y) {
        side_x += delta_x;
        map_x += step_x;
        side = 0;
      } else {
        side_y += delta_y;
        map_y += step_y;
        side = 1;
      }
      if (map_x < 0 || map_y < 0 || map_x >= kMapW || map_y >= kMapH) {
        hit = 1;
        break;
      }
      const char c = kMap[map_y][map_x];
      if (c == '#') {
        hit = 1;
        break;
      }
      if (c == 'd' || c == 'r') {
        const uint8_t id = g_door_at[map_y][map_x];
        if (id != 0xFF && g_doors[id].open < 230) {
          hit = 2;
          door_i = id;
          break;
        }
      }
    }

    if (hit == 0) {
      dither_column(buf, col, 0, kViewH);
      continue;
    }

    float perp = (side == 0) ? (side_x - delta_x) : (side_y - delta_y);
    if (perp < 0.05f) {
      perp = 0.05f;
    }
    g_z[col] = (uint16_t)fminf(perp * 256.0f, 65534.0f);

    float wall_x = (side == 0) ? (g_py + perp * ray_y) : (g_px + perp * ray_x);
    wall_x -= floorf(wall_x);
    int tex_x = (int)(wall_x * 16.0f);
    if (side == 0 && ray_x > 0.0f) {
      tex_x = 15 - tex_x;
    }
    if (side == 1 && ray_y < 0.0f) {
      tex_x = 15 - tex_x;
    }

    int shade = 0;
    if (perp > 3.2f) {
      shade = 2;
    } else if (perp > 1.6f) {
      shade = 1;
    }
    if (side == 1 && shade < 2) {
      ++shade;
    }

    const int horizon = kHorizon + g_bob;
    const int line_h = (int)((float)kViewH / perp);
    const int y_start = horizon - line_h / 2;
    const int y_end = y_start + line_h;
    dither_column(buf, col, 0, y_start);
    dither_column(buf, col, y_end, kViewH);

    const char *const *tex = kTexBrick;
    int vis_h = line_h;
    if (hit == 2 && door_i >= 0) {
      tex = g_doors[door_i].red ? kTexRed : kTexDoor;
      vis_h = line_h * (255 - g_doors[door_i].open) / 255;
    } else if (map_x >= 0 && map_y >= 0 && map_x < kMapW && map_y < kMapH) {
      tex = kWalls[(map_x * 3 + map_y) & 3];
    }
    paint_tex(buf, col, y_start, y_start + vis_h, y_start, line_h, tex, tex_x, shade);
  }

  g_bill_n = 0;
  const uint32_t now = millis();
  const float bob = sinf((float)now * 0.006f) * 0.05f;
  for (int i = 0; i < g_enemy_n; ++i) {
    const Enemy &e = g_enemies[i];
    const bool flash = e.alive && now < e.flash_until;
    if (!e.alive) {
      add_bill(e.x, e.y, kSprCorpse, 16, 16, 88, false);
    } else if (e.kind == kBeast) {
      add_bill(e.x, e.y, kSprBeast, 16, 16, 96, flash);
    } else {
      add_bill(e.x, e.y, kSprGuard, 16, 16, 80, flash);
    }
  }
  for (int i = 0; i < g_pickup_n; ++i) {
    if (!g_pickups[i].live) {
      continue;
    }
    const char *const *rows = kSprAmmo;
    if (g_pickups[i].kind == 'h') {
      rows = kSprMed;
    } else if (g_pickups[i].kind == 'v') {
      rows = kSprArmor;
    } else if (g_pickups[i].kind == 'g') {
      rows = kSprGun;
    } else if (g_pickups[i].kind == 'k') {
      rows = kSprKey;
    }
    add_bill(g_pickups[i].x, g_pickups[i].y + bob, rows, 8, 8, 40, false);
  }
  for (int i = 0; i < kMaxShot; ++i) {
    if (!g_shots[i].live) {
      continue;
    }
    add_bill(g_shots[i].x, g_shots[i].y, kSprBolt, 5, 5, 22, false);
  }
  if (g_exit_x >= 0) {
    add_bill((float)g_exit_x + 0.5f, (float)g_exit_y + 0.5f, kSprExit, 8, 8, 58, false);
  }

  for (int i = 1; i < g_bill_n; ++i) {
    const Bill key = g_bills[i];
    int j = i;
    while (j > 0 && g_bills[j - 1].depth < key.depth) {
      g_bills[j] = g_bills[j - 1];
      --j;
    }
    g_bills[j] = key;
  }
  for (int i = 0; i < g_bill_n; ++i) {
    draw_bill(buf, g_bills[i]);
  }

  draw_crosshair(buf);
  const char *const *gun = (g_shotgun && g_ammo >= 2) ? kSprShotgun : kSprPistol;
  const int gun_y = kViewH - 16 - g_kick + g_bob / 2;
  stamp(buf, gun, 16, 16, 56, gun_y);
  if (now < g_flash_until) {
    const int mx = 64;
    const int my = gun_y + 5;
    pixel(buf, mx, my, true);
    pixel(buf, mx - 1, my, true);
    pixel(buf, mx + 1, my, true);
    pixel(buf, mx, my - 1, true);
    pixel(buf, mx, my - 2, true);
    if (gun == kSprShotgun) {
      pixel(buf, mx - 3, my, true);
      pixel(buf, mx + 3, my, true);
    }
  }

  if (now < g_banner_until && g_banner[0] != '\0') {
    const int w = oled.getStrWidth(g_banner);
    const int x = (128 - w) / 2;
    for (int y = 19; y < 30; ++y) {
      for (int xx = x - 2; xx < x + w + 2; ++xx) {
        pixel(buf, xx, y, false);
      }
    }
    oled.drawStr(x, 21, g_banner);
  }

  if (g_mode == Mode::Dead) {
    for (int y = 16; y < 44; ++y) {
      for (int x = 10; x < 118; ++x) {
        pixel(buf, x, y, false);
      }
    }
    text_center(20, "YOU DIED");
    text_center(32, "touch to retry");
  } else if (g_mode == Mode::Won) {
    for (int y = 16; y < 44; ++y) {
      for (int x = 10; x < 118; ++x) {
        pixel(buf, x, y, false);
      }
    }
    text_center(20, "CLEARED");
    text_center(32, "touch to run again");
  }

  if (now < g_pain_until) {
    for (int i = 0; i < 128 * 7; ++i) {
      buf[i] ^= 0xFF;
    }
  }

  for (int x = 0; x < 128; ++x) {
    pixel(buf, x, kViewH, true);
  }
  char line[28];
  const char weapon = (g_shotgun && g_ammo >= 2) ? 'S' : 'P';
  snprintf(line, sizeof line, "H%3d A%3d %c%2d%s", g_hp, g_armor, weapon, g_ammo, g_key ? " K" : "");
  oled.drawStr(0, kViewH + 1, line);
  oled.sendBuffer();
}

bool enemy_near(float x, float y, int self) {
  for (int i = 0; i < g_enemy_n; ++i) {
    if (i == self || !g_enemies[i].alive) {
      continue;
    }
    const float dx = x - g_enemies[i].x;
    const float dy = y - g_enemies[i].y;
    if (dx * dx + dy * dy < 0.35f * 0.35f) {
      return true;
    }
  }
  return false;
}

bool player_blocked(float x, float y) {
  return block_body(x, y) || enemy_near(x, y, -1);
}

void try_move(float dx, float dy) {
  const bool stuck = player_blocked(g_px, g_py);
  if (stuck || !player_blocked(g_px + dx, g_py)) {
    g_px += dx;
  }
  if (stuck || !player_blocked(g_px, g_py + dy)) {
    g_py += dy;
  }
}

bool enemy_blocked(int self, float x, float y) {
  return block_body(x, y) || enemy_near(x, y, self);
}

void step_actor(int self, float dx, float dy, float dist, float speed, float dt) {
  if (dist < 0.001f) {
    return;
  }
  const float ux = dx / dist;
  const float uy = dy / dist;
  const float s = speed * dt;
  Enemy &e = g_enemies[self];
  const float nx = e.x + ux * s;
  const float ny = e.y + uy * s;
  if (!enemy_blocked(self, nx, ny)) {
    e.x = nx;
    e.y = ny;
  } else if (!enemy_blocked(self, nx, e.y)) {
    e.x = nx;
  } else if (!enemy_blocked(self, e.x, ny)) {
    e.y = ny;
  }
}

void hurt(int dmg) {
  if (g_mode != Mode::Play) {
    return;
  }
  const uint32_t now = millis();
  if (now < g_invuln) {
    return;
  }
  g_invuln = now + 280;
  g_pain_until = now + 90;
  if (g_armor > 0) {
    int cut = dmg / 2;
    if (cut < 1) {
      cut = 1;
    }
    if (cut > g_armor) {
      cut = g_armor;
    }
    g_armor -= cut;
    dmg -= cut;
  }
  g_hp -= dmg;
  if (g_hp <= 0) {
    g_hp = 0;
    g_mode = Mode::Dead;
  }
}

int hitscan(float ang, float radius) {
  const float dx = cosf(ang);
  const float dy = sinf(ang);
  int best = -1;
  float best_d = 12.0f;
  for (int i = 0; i < g_enemy_n; ++i) {
    if (!g_enemies[i].alive) {
      continue;
    }
    const float ex = g_enemies[i].x - g_px;
    const float ey = g_enemies[i].y - g_py;
    const float along = ex * dx + ey * dy;
    if (along < 0.25f || along >= best_d) {
      continue;
    }
    const float side = ex * (-dy) + ey * dx;
    if (fabsf(side) > radius) {
      continue;
    }
    if (!los(g_px, g_py, g_enemies[i].x, g_enemies[i].y)) {
      continue;
    }
    best = i;
    best_d = along;
  }
  return best;
}

void damage_enemy(int index, int amount) {
  Enemy &e = g_enemies[index];
  if (!e.alive) {
    return;
  }
  e.hp -= amount;
  e.flash_until = millis() + 80;
  if (e.hp <= 0) {
    e.alive = false;
  }
}

bool shot_from(int owner) {
  for (int i = 0; i < kMaxShot; ++i) {
    if (g_shots[i].live && g_shots[i].owner == (uint8_t)owner) {
      return true;
    }
  }
  return false;
}

void spawn_shot(int owner, float x, float y, float dx, float dy, float dist) {
  if (dist < 0.01f) {
    return;
  }
  int slot = -1;
  for (int i = 0; i < kMaxShot; ++i) {
    if (!g_shots[i].live) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return;
  }
  const float ux = dx / dist;
  const float uy = dy / dist;
  Shot &s = g_shots[slot];
  s.live = true;
  s.owner = (uint8_t)owner;
  s.x = x + ux * 0.45f;
  s.y = y + uy * 0.45f;
  s.vx = ux * 3.4f;
  s.vy = uy * 3.4f;
  if (block_ray((int)floorf(s.x), (int)floorf(s.y))) {
    s.live = false;
  }
}

bool cell_busy(int x, int y) {
  if ((int)floorf(g_px) == x && (int)floorf(g_py) == y) {
    return true;
  }
  for (int i = 0; i < g_enemy_n; ++i) {
    if (!g_enemies[i].alive) {
      continue;
    }
    if ((int)floorf(g_enemies[i].x) == x && (int)floorf(g_enemies[i].y) == y) {
      return true;
    }
  }
  return false;
}

void tick_doors(uint32_t now, float dt) {
  int step = (int)(dt * 460.0f);
  if (step < 1) {
    step = 1;
  }
  for (int i = 0; i < g_door_n; ++i) {
    Door &d = g_doors[i];
    if (d.vel > 0) {
      const int open = (int)d.open + step;
      if (open >= 255) {
        d.open = 255;
        d.vel = 0;
        d.hold_until = now + 2500;
      } else {
        d.open = (uint8_t)open;
      }
    } else if (d.vel < 0) {
      const int open = (int)d.open - step;
      if (open <= 0) {
        d.open = 0;
        d.vel = 0;
      } else {
        d.open = (uint8_t)open;
      }
    } else if (d.open == 255 && now >= d.hold_until && !cell_busy(d.x, d.y)) {
      d.vel = -1;
    }
  }
}

Door *door_ahead() {
  for (float dist = 0.3f; dist <= 0.95f; dist += 0.25f) {
    const int x = (int)floorf(g_px + g_dir_x * dist);
    const int y = (int)floorf(g_py + g_dir_y * dist);
    if (x < 0 || y < 0 || x >= kMapW || y >= kMapH) {
      continue;
    }
    const char c = kMap[y][x];
    if (c != 'd' && c != 'r') {
      continue;
    }
    const uint8_t id = g_door_at[y][x];
    if (id != 0xFF) {
      return &g_doors[id];
    }
  }
  return nullptr;
}

void operate(Door &door) {
  if (door.vel > 0) {
    door.vel = -1;
    return;
  }
  if (door.vel < 0) {
    door.vel = 1;
    return;
  }
  if (door.open == 0) {
    if (door.red && !g_key) {
      say("NEED KEY", 800);
      return;
    }
    door.vel = 1;
  } else {
    door.vel = -1;
  }
}

void take_pickups() {
  for (int i = 0; i < g_pickup_n; ++i) {
    Pickup &p = g_pickups[i];
    if (!p.live) {
      continue;
    }
    const float dx = p.x - g_px;
    const float dy = p.y - g_py;
    if (dx * dx + dy * dy > 0.30f * 0.30f) {
      continue;
    }
    if (p.kind == 'a') {
      if (g_ammo >= 99) {
        continue;
      }
      g_ammo += 10;
      if (g_ammo > 99) {
        g_ammo = 99;
      }
    } else if (p.kind == 'h') {
      if (g_hp >= 100) {
        continue;
      }
      g_hp += 25;
      if (g_hp > 100) {
        g_hp = 100;
      }
    } else if (p.kind == 'v') {
      if (g_armor >= 100) {
        continue;
      }
      g_armor += 50;
      if (g_armor > 100) {
        g_armor = 100;
      }
    } else if (p.kind == 'k') {
      g_key = true;
      say("RED KEY", 900);
    } else if (p.kind == 'g') {
      g_shotgun = true;
      g_ammo += 8;
      if (g_ammo > 99) {
        g_ammo = 99;
      }
      say("SHOTGUN", 900);
    } else {
      continue;
    }
    p.live = false;
  }
}

void move_shots(float dt) {
  for (int i = 0; i < kMaxShot; ++i) {
    Shot &s = g_shots[i];
    if (!s.live) {
      continue;
    }
    const float nx = s.x + s.vx * dt;
    const float ny = s.y + s.vy * dt;
    if (block_ray((int)floorf(nx), (int)floorf(ny))) {
      s.live = false;
      continue;
    }
    s.x = nx;
    s.y = ny;
    const float dx = s.x - g_px;
    const float dy = s.y - g_py;
    if (dx * dx + dy * dy < 0.22f * 0.22f) {
      s.live = false;
      hurt(8);
    }
  }
}

void think_enemies(uint32_t now, float dt) {
  for (int i = 0; i < g_enemy_n; ++i) {
    if (g_mode != Mode::Play) {
      return;
    }
    Enemy &e = g_enemies[i];
    if (!e.alive) {
      continue;
    }
    const float pdx = g_px - e.x;
    const float pdy = g_py - e.y;
    const float pdist = sqrtf(pdx * pdx + pdy * pdy);
    const bool see = pdist < 8.0f && los(e.x, e.y, g_px, g_py);
    if (see) {
      e.lx = g_px;
      e.ly = g_py;
      e.saw_until = now + 2200;
    }
    const bool hunt = see || now < e.saw_until;
    const float speed = (e.kind == kBeast) ? 1.65f : 1.15f;
    if (e.kind == kGuard) {
      if (see && pdist > 2.8f) {
        step_actor(i, pdx, pdy, pdist, speed, dt);
      } else if (see && pdist < 1.7f) {
        step_actor(i, -pdx, -pdy, pdist, speed, dt);
      } else if (!see && hunt) {
        const float dx = e.lx - e.x;
        const float dy = e.ly - e.y;
        step_actor(i, dx, dy, sqrtf(dx * dx + dy * dy), speed, dt);
      }
      if (see && pdist < 5.0f && pdist > 1.3f && now >= e.next_act && !shot_from(i)) {
        spawn_shot(i, e.x, e.y, pdx, pdy, pdist);
        e.next_act = now + 1100;
        e.flash_until = now + 120;
      }
    } else if (hunt) {
      const float dx = (see ? g_px : e.lx) - e.x;
      const float dy = (see ? g_py : e.ly) - e.y;
      step_actor(i, dx, dy, sqrtf(dx * dx + dy * dy), speed, dt);
      if (see && pdist < 0.75f && now >= e.next_act) {
        hurt(15);
        e.next_act = now + 700;
        e.flash_until = now + 140;
      }
    }
  }
}

void player_fire(uint32_t now, const Input &input) {
  if (g_kick > 0) {
    --g_kick;
  }
  if (!input.fire_held || now < g_next_shot || g_mode != Mode::Play) {
    return;
  }
  if (g_shotgun && g_ammo >= 2) {
    const float spread[] = {-0.18f, -0.09f, 0.0f, 0.09f, 0.18f};
    for (float s : spread) {
      const int hit = hitscan(g_ang + s, 0.16f);
      if (hit >= 0) {
        damage_enemy(hit, 10);
      }
    }
    g_ammo -= 2;
    g_next_shot = now + 820;
  } else if (g_ammo >= 1) {
    const int hit = hitscan(g_ang, 0.30f);
    if (hit >= 0) {
      damage_enemy(hit, 12);
    }
    g_ammo -= 1;
    g_next_shot = now + 340;
  } else {
    if (now >= g_banner_until) {
      say("NO AMMO", 500);
    }
    g_next_shot = now + 250;
    return;
  }
  g_kick = 6;
  g_flash_until = now + 70;
}

}  // namespace

void game_setup() {
  check_art();
  g_mode = Mode::Title;
}

void game_step(const Input &input) {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  float dt = (last_ms == 0) ? 0.016f : (float)(now - last_ms) / 1000.0f;
  last_ms = now;
  if (dt > 0.05f) {
    dt = 0.05f;
  }
  if (dt < 0.0f) {
    dt = 0.0f;
  }

  if (g_mode != Mode::Play) {
    if (input.fire_edge || input.use_edge) {
      begin_level();
    }
    return;
  }
  if (g_map) {
    if (input.use_edge) {
      g_map = false;
    }
    return;
  }

  const float forward = -(float)input.axis_y / 1400.0f;
  const float turn = (float)input.axis_x / 1400.0f;
  g_ang += turn * 2.7f * dt;
  while (g_ang >= kTau) {
    g_ang -= kTau;
  }
  while (g_ang < 0.0f) {
    g_ang += kTau;
  }
  aim();
  const float step = forward * 2.15f * dt;
  try_move(g_dir_x * step, g_dir_y * step);
  if (fabsf(forward) > 0.12f) {
    g_bob_phase += dt * 12.0f * fabsf(forward);
  }
  g_bob = (int)(sinf(g_bob_phase) * 2.0f);

  tick_doors(now, dt);
  take_pickups();
  if (g_exit_x >= 0 && (int)floorf(g_px) == g_exit_x && (int)floorf(g_py) == g_exit_y) {
    g_mode = Mode::Won;
    return;
  }
  move_shots(dt);
  if (g_mode != Mode::Play) {
    return;
  }
  think_enemies(now, dt);
  if (g_mode != Mode::Play) {
    return;
  }
  player_fire(now, input);
  if (input.use_edge) {
    if (Door *door = door_ahead()) {
      operate(*door);
    } else {
      g_map = true;
    }
  }
}

void game_draw() {
  if (g_mode == Mode::Title) {
    draw_title();
    return;
  }
  if (g_map && g_mode == Mode::Play) {
    draw_map();
    return;
  }
  draw_world();
}
