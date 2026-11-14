#include "game.h"

#include "art.h"
#include "oled.h"
#include "title_logo.h"

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kMapW = 16;
constexpr int kMapH = 16;
constexpr int kLevelCount = 5;
constexpr int kViewW = 128;
constexpr int kViewH = 56;
constexpr int kHorizon = 22;
constexpr float kPlane = 0.80f;
constexpr float kTau = 6.2831853f;
constexpr int kMaxEnemy = 8;
constexpr int kMaxPickup = 12;
constexpr int kMaxShot = 6;
constexpr uint8_t kBayer4x4[16] = {
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5,
};

// One byte per cell. Reserve 0x10-0x1F for wall materials, 0x20-0x2F for
// mechanisms, 0x30-0x3F for actors, 0x40-0x4F for pickups, and 0x50-0x5F for exits.
enum MapCell : uint8_t {
  kCellFloor = 0x00,
  kCellBrickWall = 0x10,
  kCellGate = 0x22,
  kCellPlayerStart = 0x30,
  kCellGuardSpawn = 0x31,
  kCellDemonSpawn = 0x32,
  kCellAmmo = 0x40,
  kCellMedkit = 0x41,
  kCellArmor = 0x42,
  kCellShotgun = 0x43,
  kCellExit = 0x50,
};

struct LevelDef {
  const uint8_t (*cells)[kMapW];
  float start_angle;
  const char *name;
};

// Row-major byte maps; comments show the original character-grid layout.
// +Y points south and a start angle of zero faces east.
const uint8_t kMapData[kLevelCount][kMapH][kMapW] = {
  {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x30, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ###@.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####....a.######
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x31, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #######e#.######
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #######...######
    {0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #a........######
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x41, 0x32, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####hm#########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x10}, // ###v.........G.#
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
  },
  {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x30, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##@...##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####....a.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x00, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // #....e.....#####
    {0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ###.############
    {0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ###.############
    {0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ###.############
    {0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x10}, // ###..........G.#
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
  },
  {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10}, // ##@..........###
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10}, // ############.###
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10}, // ############.###
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10}, // ####....a....###
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####...e...#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x22, 0x00, 0x10}, // ##########...G.#
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
  },
  {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x30, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##@...##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x41, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.h.########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #######.########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #######.########
    {0x10, 0x10, 0x00, 0x00, 0x00, 0x42, 0x00, 0x31, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##...v.e...#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##########.#####
    {0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x10}, // ###..........G.#
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
  },
  {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x30, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ##@...##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // #####.##########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####...#########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x31, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####...e.#######
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x40, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####....a#######
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ####.###########
    {0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x50, 0x10, 0x10}, // ####.....h...x##
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // ################
  },
};

const LevelDef kLevels[kLevelCount] = {
    {kMapData[0], 0.0f, "MAP 1"},
    {kMapData[1], 0.0f, "MAP 2"},
    {kMapData[2], 0.0f, "MAP 3"},
    {kMapData[3], 0.0f, "MAP 4"},
    {kMapData[4], 0.0f, "MAP 5"},
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
  uint32_t attack_until;
  Kind kind;
  bool alive;
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

enum class Mode : uint8_t { Title, Menu, Play, Dead, Won };
enum class RunMode : uint8_t { Levels, Infinite };
enum class TransitionPhase : uint8_t { Out, In };

Mode g_mode = Mode::Title;
RunMode g_run_mode = RunMode::Levels;
bool g_map = false;
uint8_t g_level = 0;
uint8_t g_runtime_map[kMapH][kMapW];
uint32_t g_rng = 1;
uint32_t g_score = 0;
uint32_t g_best_score = 0;
uint32_t g_infinite_map = 1;
uint32_t g_menu_repeat_at = 0;
uint8_t g_menu_choice = 0;
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
bool g_shotgun = false;
int g_exit_x = -1;
int g_exit_y = -1;
int g_gate_x = -1;
int g_gate_y = -1;
uint8_t g_gate_open = 0;
bool g_gate_started = false;
uint32_t g_gate_open_started = 0;
bool g_transitioning = false;
bool g_transition_to_won = false;
TransitionPhase g_transition_phase = TransitionPhase::Out;
uint32_t g_transition_started = 0;
uint32_t g_next_shot = 0;
uint32_t g_flash_until = 0;
uint32_t g_pain_until = 0;
uint32_t g_invuln = 0;
uint32_t g_banner_until = 0;
char g_banner[16] = "";

Enemy g_enemies[kMaxEnemy];
int g_enemy_n = 0;
Pickup g_pickups[kMaxPickup];
int g_pickup_n = 0;
Shot g_shots[kMaxShot];
uint16_t g_z[kViewW];
Bill g_bills[24];
int g_bill_n = 0;

uint32_t rng_next() {
  uint32_t x = g_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_rng = x ? x : 0xA341316Cu;
  return g_rng;
}

void add_score(uint32_t points) {
  if (0xFFFFFFFFu - g_score < points) {
    g_score = 0xFFFFFFFFu;
  } else {
    g_score += points;
  }
}

void save_best_score() {
  if (g_run_mode != RunMode::Infinite || g_score <= g_best_score) {
    return;
  }
  g_best_score = g_score;
  Preferences prefs;
  if (prefs.begin("minidoom", false)) {
    prefs.putUInt("inf_best", g_best_score);
    prefs.end();
  }
}

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

uint8_t map_at(int x, int y) {
  if (x < 0 || y < 0 || x >= kMapW || y >= kMapH) {
    return kCellBrickWall;
  }
  return g_run_mode == RunMode::Infinite ? g_runtime_map[y][x]
                                         : kLevels[g_level].cells[y][x];
}

bool block_ray(int x, int y) {
  const uint8_t cell = map_at(x, y);
  return (cell >= 0x10 && cell <= 0x1F) ||
         ((cell == kCellGate || cell == kCellExit) && g_gate_open < 100);
}

int camera_horizon() {
  return kHorizon + g_bob;
}

float camera_z() {
  return 0.5f;
}

bool block_body(float x, float y) {
  const float r = 0.18f;
  const float px[4] = {x - r, x + r, x - r, x + r};
  const float py[4] = {y - r, y - r, y + r, y + r};
  for (int i = 0; i < 4; ++i) {
    const int cx = (int)floorf(px[i]);
    const int cy = (int)floorf(py[i]);
    if (block_ray(cx, cy)) {
      return true;
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
  const bool ok = art_width(kTexWall, 16, 16) && art_width(kTexGate, 16, 16) &&
                  art_width(kSprGuard, 16, 16) && art_width(kSprBeast, 16, 16) &&
                  art_width(kSprGuardWalk, 16, 16) && art_width(kSprGuardAttack, 16, 16) &&
                  art_width(kSprBeastWalk, 16, 16) && art_width(kSprBeastAttack, 16, 16) &&
                  art_width(kSprCorpse, 16, 16) && art_width(kSprPistol, 16, 16) &&
                  art_width(kSprShotgun, 16, 16) &&
                  art_width(kSprAmmo, 8, 8) && art_width(kSprMed, 8, 8) &&
                  art_width(kSprArmor, 8, 8) && art_width(kSprGun, 8, 8) &&
                  art_width(kSprExit, 8, 8) && art_width(kSprBolt, 5, 5);
  if (!ok) {
    Serial.println("sprite rows are the wrong width");
  }
}

void clear_active_map_state() {
  g_map = false;
  g_enemy_n = 0;
  g_pickup_n = 0;
  g_exit_x = -1;
  g_exit_y = -1;
  g_gate_x = -1;
  g_gate_y = -1;
  g_gate_open = 0;
  g_gate_started = false;
  g_gate_open_started = 0;
  memset(g_enemies, 0, sizeof g_enemies);
  memset(g_pickups, 0, sizeof g_pickups);
  memset(g_shots, 0, sizeof g_shots);
  g_bob = 0;
  g_bob_phase = 0;
  g_kick = 0;
}

bool find_open_floor(int &out_x, int &out_y) {
  for (int attempt = 0; attempt < 256; ++attempt) {
    const int x = 1 + (int)(rng_next() % 14u);
    const int y = 1 + (int)(rng_next() % 14u);
    if (map_at(x, y) != kCellFloor) continue;
    const float dx = (float)x + 0.5f - g_px;
    const float dy = (float)y + 0.5f - g_py;
    if (dx * dx + dy * dy < 5.0f) continue;
    bool occupied = false;
    for (int i = 0; i < g_enemy_n; ++i) {
      const float ex = (float)x + 0.5f - g_enemies[i].x;
      const float ey = (float)y + 0.5f - g_enemies[i].y;
      if (ex * ex + ey * ey < 1.0f) occupied = true;
    }
    for (int i = 0; i < g_pickup_n; ++i) {
      const float px = (float)x + 0.5f - g_pickups[i].x;
      const float py = (float)y + 0.5f - g_pickups[i].y;
      if (g_pickups[i].live && px * px + py * py < 1.0f) occupied = true;
    }
    if (occupied) continue;
    out_x = x;
    out_y = y;
    return true;
  }
  return false;
}

void add_enemy_at(int x, int y, Kind kind, uint32_t now) {
  if (g_enemy_n >= kMaxEnemy) return;
  Enemy &e = g_enemies[g_enemy_n];
  e = {};
  e.x = (float)x + 0.5f;
  e.y = (float)y + 0.5f;
  e.lx = e.x;
  e.ly = e.y;
  e.kind = kind;
  e.hp = (kind == kBeast) ? 64 : 30;
  e.alive = true;
  e.next_act = now + (uint32_t)g_enemy_n * 350u;
  ++g_enemy_n;
}

void add_pickup_at(int x, int y, char kind) {
  if (g_pickup_n >= kMaxPickup) return;
  Pickup &p = g_pickups[g_pickup_n++];
  p.x = (float)x + 0.5f;
  p.y = (float)y + 0.5f;
  p.kind = kind;
  p.live = true;
}

void ensure_map_requirements(uint32_t now) {
  int x = 0;
  int y = 0;
  while (g_enemy_n < 2 && find_open_floor(x, y)) {
    add_enemy_at(x, y, g_enemy_n == 0 ? kBeast : kGuard, now);
  }
  bool have_health = false;
  bool have_ammo = false;
  for (int i = 0; i < g_pickup_n; ++i) {
    have_health |= g_pickups[i].kind == 'h';
    have_ammo |= g_pickups[i].kind == 'a';
  }
  if (!have_health && find_open_floor(x, y)) add_pickup_at(x, y, 'h');
  if (!have_ammo && find_open_floor(x, y)) add_pickup_at(x, y, 'a');
}

void activate_map() {
  clear_active_map_state();
  const uint32_t now = millis();
  for (int y = 0; y < kMapH; ++y) {
    for (int x = 0; x < kMapW; ++x) {
      const uint8_t cell = map_at(x, y);
      const float cx = (float)x + 0.5f;
      const float cy = (float)y + 0.5f;
      if (cell == kCellPlayerStart) {
        g_px = cx;
        g_py = cy;
      } else if ((cell == kCellGuardSpawn || cell == kCellDemonSpawn) &&
                 g_enemy_n < kMaxEnemy) {
        add_enemy_at(x, y, cell == kCellDemonSpawn ? kBeast : kGuard, now);
      } else if ((cell == kCellAmmo || cell == kCellMedkit ||
                  cell == kCellArmor || cell == kCellShotgun) &&
                 g_pickup_n < kMaxPickup) {
        add_pickup_at(x, y, cell == kCellAmmo ? 'a' :
                           cell == kCellMedkit ? 'h' :
                           cell == kCellArmor ? 'v' : 'g');
      } else if (cell == kCellExit) {
        g_exit_x = x;
        g_exit_y = y;
        g_gate_x = x;
        g_gate_y = y;
      } else if (cell == kCellGate) {
        g_gate_x = x;
        g_gate_y = y;
      }
    }
  }
  ensure_map_requirements(now);
  g_next_shot = now + 400;
  g_mode = Mode::Play;
  aim();
  if (g_run_mode == RunMode::Infinite) {
    Serial.printf("Infinite map %lu: %d enemies\n", (unsigned long)g_infinite_map, g_enemy_n);
  } else {
    Serial.printf("%s: %d enemies\n", kLevels[g_level].name, g_enemy_n);
  }
}

void load_level(uint8_t level) {
  if (level >= kLevelCount) {
    g_mode = Mode::Won;
    return;
  }
  g_run_mode = RunMode::Levels;
  g_level = level;
  g_ang = kLevels[g_level].start_angle;
  activate_map();
}

void carve_h(int y, int x0, int x1) {
  if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
  for (int x = x0; x <= x1; ++x) g_runtime_map[y][x] = kCellFloor;
}

void carve_v(int x, int y0, int y1) {
  if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
  for (int y = y0; y <= y1; ++y) g_runtime_map[y][x] = kCellFloor;
}

void generate_infinite_map() {
  clear_active_map_state();
  g_run_mode = RunMode::Infinite;
  g_level = 0;
  g_exit_x = -1;
  g_exit_y = -1;
  g_px = 2.5f;
  g_py = 1.5f;
  memset(g_runtime_map, kCellBrickWall, sizeof g_runtime_map);
  const int a = 4 + (int)(rng_next() % 4u);
  const int b = 8 + (int)(rng_next() % 5u);
  const int c = 3 + (int)(rng_next() % 4u);
  carve_h(1, 2, a);
  carve_v(a, 1, 4);
  carve_h(4, a, b);
  carve_v(b, 4, 7);
  carve_h(7, c, b);
  carve_v(c, 7, 11);
  carve_h(11, c, 14);
  carve_v(a + (int)(rng_next() % (uint32_t)(b - a + 1)), 2, 3);
  carve_v(c + (int)(rng_next() % (uint32_t)(10 - c)), 8, 10);
  g_runtime_map[1][2] = kCellPlayerStart;
  g_runtime_map[11][13] = kCellGate;
  g_px = 2.5f;
  g_py = 1.5f;
  g_ang = 0.0f;

  const uint32_t count = (g_infinite_map - 1u) / 3u;
  const uint8_t enemy_target = (uint8_t)(2u + (count > 4u ? 4u : count));
  for (uint8_t i = 0; i < enemy_target; ++i) {
    int x = 0, y = 0;
    if (!find_open_floor(x, y)) break;
    g_runtime_map[y][x] = (i == 0 || (rng_next() & 1u))
                              ? kCellDemonSpawn : kCellGuardSpawn;
  }
  const uint8_t guaranteed_pickups[2] = {kCellMedkit, kCellAmmo};
  for (uint8_t cell : guaranteed_pickups) {
    int x = 0, y = 0;
    if (find_open_floor(x, y)) g_runtime_map[y][x] = cell;
  }
  activate_map();
}

void begin_level() {
  g_run_mode = RunMode::Levels;
  g_hp = 100;
  g_armor = 0;
  g_ammo = 24;
  g_shotgun = false;
  g_banner[0] = '\0';
  g_banner_until = 0;
  g_pain_until = 0;
  g_invuln = 0;
  g_flash_until = 0;
  g_score = 0;
  g_infinite_map = 1;
  g_px = 1.5f;
  g_py = 1.5f;
  load_level(0);
}

void begin_infinite() {
  g_run_mode = RunMode::Infinite;
  g_hp = 100;
  g_armor = 0;
  g_ammo = 24;
  g_shotgun = false;
  g_banner[0] = '\0';
  g_banner_until = 0;
  g_pain_until = 0;
  g_invuln = 0;
  g_flash_until = 0;
  g_score = 0;
  g_infinite_map = 1;
  g_rng ^= millis() + micros() + 0x9E3779B9u;
  if (g_rng == 0) g_rng = 1;
  generate_infinite_map();
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

void draw_hud_icon(uint8_t *buf, int x, int y, const char *const *rows) {
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 7; ++col) {
      if (rows[row][col] == '#') {
        pixel(buf, x + col, y + row, true);
      }
    }
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

bool title_logo_pixel(int x, int y) {
  if (x < 0 || x >= 112 || y < 0 || y >= 48) {
    return false;
  }
  const uint8_t packed = kTitleLogo[y * 14 + x / 8];
  return (packed & (1u << (x & 7))) != 0;
}

void draw_gradient_logo() {
  constexpr int kLogoX = 8;
  constexpr int kLogoY = 2;
  for (int y = 0; y < 48; ++y) {
    // Ordered dithering fades the logo's interior from clear at the top to
    // fully filled at the bottom. Its silhouette stays crisp and solid.
    const int fill_level = (y * 16) / 47;
    for (int x = 0; x < 112; ++x) {
      if (!title_logo_pixel(x, y)) {
        continue;
      }
      bool border = false;
      for (int dy = -1; dy <= 1 && !border; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if ((dx != 0 || dy != 0) && !title_logo_pixel(x + dx, y + dy)) {
            border = true;
            break;
          }
        }
      }
      const uint8_t rank = kBayer4x4[(y & 3) * 4 + (x & 3)];
      if (border || rank < fill_level) {
        oled.drawPixel(kLogoX + x, kLogoY + y);
      }
    }
  }
}

void draw_title() {
  oled.clearBuffer();
  draw_gradient_logo();
  text_center(56, "CLICK TO CONTINUE");
  oled.sendBuffer();
}

void draw_menu() {
  oled.clearBuffer();
  // The OLED uses top-positioned 5x7 text, so keep each row clear of its frame.
  oled.drawStr(43, 1, "DOOM MODE");
  oled.drawHLine(22, 10, 84);
  if (g_menu_choice == 0) oled.drawFrame(17, 15, 94, 13);
  if (g_menu_choice == 1) oled.drawFrame(17, 31, 94, 13);
  oled.drawStr(27, 18, g_menu_choice == 0 ? ">" : " ");
  oled.drawStr(41, 18, "LEVELS");
  oled.drawStr(27, 34, g_menu_choice == 1 ? ">" : " ");
  oled.drawStr(41, 34, "INFINITE");
  oled.drawHLine(22, 46, 84);
  char best[20];
  snprintf(best, sizeof best, "BEST %lu", (unsigned long)g_best_score);
  text_center(49, best);
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
  constexpr int kCell = 3;
  constexpr int kMapX = 40;
  constexpr int kMapY = 8;
  constexpr int kMapPixels = kMapW * kCell;

  // Framed center map with compact status panels on either side.
  for (int i = 0; i < kMapPixels + 2; ++i) {
    pixel(buf, kMapX - 1 + i, kMapY - 1, true);
    pixel(buf, kMapX - 1 + i, kMapY + kMapPixels, true);
  }
  for (int i = 0; i < kMapPixels + 2; ++i) {
    pixel(buf, kMapX - 1, kMapY - 1 + i, true);
    pixel(buf, kMapX + kMapPixels, kMapY - 1 + i, true);
  }

  for (int y = 0; y < kMapH; ++y) {
    for (int x = 0; x < kMapW; ++x) {
      const uint8_t cell = map_at(x, y);
      const int x0 = kMapX + x * kCell;
      const int y0 = kMapY + y * kCell;
      if (cell >= 0x10 && cell <= 0x1F) {
        fill_box(buf, x0, y0, kCell, kCell);
      } else if (cell == kCellGate || cell == kCellExit) {
        pixel(buf, x0, y0, true);
        pixel(buf, x0 + 1, y0, true);
        pixel(buf, x0 + 2, y0, true);
        pixel(buf, x0, y0 + 1, true);
        pixel(buf, x0 + 2, y0 + 1, true);
        pixel(buf, x0, y0 + 2, true);
        pixel(buf, x0 + 1, y0 + 2, true);
        pixel(buf, x0 + 2, y0 + 2, true);
      }
    }
  }
  for (int i = 0; i < g_pickup_n; ++i) {
    if (!g_pickups[i].live) {
      continue;
    }
    const int x = kMapX + (int)(g_pickups[i].x * kCell);
    const int y = kMapY + (int)(g_pickups[i].y * kCell);
    pixel(buf, x, y, true);
  }
  int alive_enemies = 0;
  for (int i = 0; i < g_enemy_n; ++i) {
    if (!g_enemies[i].alive) {
      continue;
    }
    ++alive_enemies;
    const int x = kMapX + (int)(g_enemies[i].x * kCell);
    const int y = kMapY + (int)(g_enemies[i].y * kCell);
    if (g_enemies[i].kind == kBeast) {
      fill_box(buf, x - 1, y - 1, 3, 3);
    } else {
      pixel(buf, x, y, true);
    }
  }
  const int px = kMapX + (int)(g_px * kCell);
  const int py = kMapY + (int)(g_py * kCell);
  if (((millis() / 220) & 1u) == 0) {
    pixel(buf, px, py, true);
    const int fx = (int)lroundf(cosf(g_ang));
    const int fy = (int)lroundf(sinf(g_ang));
    pixel(buf, px + fx, py + fy, true);
  }

  char label[12];
  snprintf(label, sizeof label, "MAP %u/5", (unsigned)(g_level + 1));
  oled.drawStr(2, 0, label);
  oled.drawStr(80, 0, "SW CLOSE");

  oled.drawStr(2, 10, "YOU");
  pixel(buf, 3, 21, true);
  pixel(buf, 4, 21, true);
  char enemy_label[8];
  snprintf(enemy_label, sizeof enemy_label, "EN %02d", alive_enemies);
  oled.drawStr(2, 29, enemy_label);
  oled.drawStr(2, 41, "GOAL");
  oled.drawStr(2, 50, "GATE");

  oled.drawStr(94, 10, "HP");
  for (int x = 0; x < 30; ++x) {
    pixel(buf, 94 + x, 19, true);
    pixel(buf, 94 + x, 23, true);
  }
  for (int y = 19; y <= 23; ++y) {
    pixel(buf, 94, y, true);
    pixel(buf, 123, y, true);
  }
  const int hp_fill = (g_hp * 26) / 100;
  fill_box(buf, 96, 21, hp_fill, 1);
  char stat[8];
  snprintf(stat, sizeof stat, "%3d", g_hp);
  oled.drawStr(94, 26, stat);
  snprintf(stat, sizeof stat, "AM %02d", g_ammo);
  oled.drawStr(94, 36, stat);
  snprintf(stat, sizeof stat, "AR %02d", g_armor);
  oled.drawStr(94, 47, stat);
  oled.sendBuffer();
}

void dither_column(uint8_t *buf, int x, int y0, int y1) {
  if (y0 < 0) {
    y0 = 0;
  }
  if (y1 > kViewH) {
    y1 = kViewH;
  }
  const int horizon = camera_horizon();
  for (int y = y0; y < y1; ++y) {
    const bool floor = y >= horizon;
    const int band = floor ? y - horizon : horizon - y;
    const uint8_t threshold = floor
        ? (band > 24 ? 13 : (band > 14 ? 10 : (band > 6 ? 7 : 4)))
        : (band > 18 ? 8 : (band > 9 ? 5 : 3));
    const uint8_t rank = kBayer4x4[(y & 3) * 4 + (x & 3)];
    const bool floor_seam = floor && band > 5 && (band % 8 == 0) && ((x & 3) != 0);
    if (rank < threshold || floor_seam) {
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
  const uint8_t threshold = (shade >= 2) ? 12 : ((shade == 1) ? 8 : 0);
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
    const uint8_t rank = kBayer4x4[(y & 3) * 4 + (x & 3)];
    if (rank < threshold) {
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
  const int horizon = camera_horizon();
  const float local_eye_z = camera_z();
  const int bottom = horizon + (int)(local_eye_z * (float)column_h);
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
  const int y = camera_horizon();
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
    uint8_t hit_cell = kCellBrickWall;
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
        hit_cell = kCellBrickWall;
        hit = 1;
        break;
      }
      const uint8_t cell = map_at(map_x, map_y);
      if ((cell >= 0x10 && cell <= 0x1F) ||
          ((cell == kCellGate || cell == kCellExit) && g_gate_open < 100)) {
        hit_cell = cell;
        hit = 1;
        break;
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

    const float hit_x = g_px + perp * ray_x;
    const float hit_y = g_py + perp * ray_y;
    float wall_x = (side == 0) ? hit_y : hit_x;
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

    const int line_h = (int)((float)kViewH / perp);
    const int horizon = camera_horizon();
    const float local_eye_z = camera_z();
    const int y_start = horizon + (int)((local_eye_z - 1.0f) * (float)line_h);
    const int y_end = horizon + (int)(local_eye_z * (float)line_h);
    const bool gate_hit = hit_cell == kCellGate || hit_cell == kCellExit;
    const int gate_lift = gate_hit ? (line_h * (int)g_gate_open) / 100 : 0;
    const int gate_bottom = y_end - gate_lift;
    const char *const *texture = gate_hit ? kTexGate : kTexWall;
    dither_column(buf, col, 0, y_start);
    dither_column(buf, col, gate_hit ? gate_bottom : y_end, kViewH);
    paint_tex(buf, col, y_start, gate_hit ? gate_bottom : y_end,
              y_start - gate_lift, line_h, texture, tex_x, shade);
  }
  g_bill_n = 0;
  const uint32_t now = millis();
  const float bob = sinf((float)now * 0.006f) * 0.05f;
  for (int i = 0; i < g_enemy_n; ++i) {
    const Enemy &e = g_enemies[i];
    const bool flash = e.alive && now < e.flash_until;
    if (!e.alive) {
      add_bill(e.x, e.y, kSprCorpse, 16, 16, 88, false);
    } else {
      const bool attacking = now < e.attack_until;
      const bool walking = now < e.saw_until && ((now / 220u + (uint32_t)i) & 1u);
      const char *const *rows;
      int sprite_w = 16;
      if (e.kind == kBeast) {
        if (attacking) {
          rows = kSprBeastAttack;
        } else if (walking) {
          rows = kSprBeastWalk;
        } else {
          rows = kSprBeast;
        }
        add_bill(e.x, e.y, rows, sprite_w, 16, 96, flash);
      } else {
        rows = attacking ? kSprGuardAttack : (walking ? kSprGuardWalk : kSprGuard);
        add_bill(e.x, e.y, rows, 16, 16, 80, flash);
      }
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
    static const char *const kMuzzleFlash[] = {
        "...#...", "..###..", ".#####.", "###.###", ".#####.", "..###..", "...#...",
    };
    const int flash_x = 61;
    const int flash_y = gun_y - 2;
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 7; ++col) {
        if (kMuzzleFlash[row][col] == '#') {
          pixel(buf, flash_x + col, flash_y + row, true);
        }
      }
    }
  }

  if (now < g_banner_until && g_banner[0] != '\0') {
    const int w = oled.getStrWidth(g_banner);
    const int x = (128 - w) / 2;
    // Keep the level label in the sky band; a center-screen banner hides
    // the horizon and makes the newly loaded floor look missing.
    for (int y = 0; y < 9; ++y) {
      for (int xx = x - 2; xx < x + w + 2; ++xx) {
        pixel(buf, xx, y, false);
      }
    }
    oled.drawStr(x, 1, g_banner);
  }

  if (g_mode == Mode::Dead) {
    for (int y = 16; y < 44; ++y) {
      for (int x = 10; x < 118; ++x) {
        pixel(buf, x, y, false);
      }
    }
    text_center(20, "YOU DIED");
    text_center(32, "fire to retry");
  } else if (g_mode == Mode::Won) {
    for (int y = 16; y < 44; ++y) {
      for (int x = 10; x < 118; ++x) {
        pixel(buf, x, y, false);
      }
    }
    text_center(20, "CLEARED");
    text_center(32, "fire to run again");
  }

  if (now < g_pain_until) {
    for (int i = 0; i < 128 * 7; ++i) {
      buf[i] ^= 0xFF;
    }
  }

  for (int x = 0; x < 128; ++x) {
    pixel(buf, x, kViewH, true);
  }
  static const char *const kHealthIcon[] = {
      ".##.##.", "#######", "#######", ".#####.", "..###..", "...#...", ".......",
  };
  static const char *const kArmorIcon[] = {
      "...#...", "..###..", ".#####.", "#######", ".#####.", "..###..", "...#...",
  };
  static const char *const kAmmoIcon[] = {
      "..###..", "..###..", ".#####.", "#######", "..###..", "..###..", "..###..",
  };
  draw_hud_icon(buf, 2, kViewH + 1, kHealthIcon);
  draw_hud_icon(buf, 42, kViewH + 1, kArmorIcon);
  draw_hud_icon(buf, 83, kViewH + 1, kAmmoIcon);
  char health[4];
  char armor[4];
  char ammo[5];
  snprintf(health, sizeof health, "%3d", g_hp);
  snprintf(armor, sizeof armor, "%3d", g_armor);
  const char weapon = (g_shotgun && g_ammo >= 2) ? 'S' : 'P';
  snprintf(ammo, sizeof ammo, "%c%02d", weapon, g_ammo);
  oled.drawStr(11, kViewH + 1, health);
  oled.drawStr(51, kViewH + 1, armor);
  oled.drawStr(92, kViewH + 1, ammo);
  if (g_run_mode == RunMode::Infinite) {
    // Keep infinite-run progress in the sky band, clear of the status strip.
    for (int y = 0; y < 9; ++y) {
      for (int x = 0; x < 62; ++x) pixel(buf, x, y, false);
    }
    char progress[24];
    snprintf(progress, sizeof progress, "I%lu S%lu",
             (unsigned long)g_infinite_map, (unsigned long)g_score);
    oled.drawStr(2, 1, progress);
  }
  if (g_transitioning) {
    uint32_t elapsed = now - g_transition_started;
    if (elapsed > 320u) elapsed = 320u;
    const uint8_t step = (uint8_t)(elapsed * 16u / 320u);
    const uint8_t erase_below = g_transition_phase == TransitionPhase::Out
                                    ? step : (uint8_t)(16u - step);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 128; ++x) {
        const uint8_t rank = kBayer4x4[(y & 3) * 4 + (x & 3)];
        if (rank < erase_below) pixel(buf, x, y, false);
      }
    }
  }
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

void update_gate(uint32_t now) {
  if (g_gate_x < 0 || g_gate_open >= 100) return;
  const float dx = (float)g_gate_x + 0.5f - g_px;
  const float dy = (float)g_gate_y + 0.5f - g_py;
  if (!g_gate_started && dx * dx + dy * dy <= 3.2f * 3.2f) {
    g_gate_started = true;
    g_gate_open_started = now;
    say("GATE ACTIVATED", 1000);
  }
  if (g_gate_started) {
    const uint32_t elapsed = now - g_gate_open_started;
    g_gate_open = elapsed >= 950 ? 100 : (uint8_t)(elapsed * 100u / 950u);
    if (g_gate_open == 100) say("GATE OPEN", 700);
  }
}

bool use_gate_if_entered(int old_x, int old_y) {
  const int new_x = (int)floorf(g_px);
  const int new_y = (int)floorf(g_py);
  const uint8_t cell = map_at(new_x, new_y);
  const bool level_exit = cell == kCellGate || cell == kCellExit;
  if (!level_exit || g_gate_open < 100 ||
      (old_x == new_x && old_y == new_y)) {
    return false;
  }

  g_transitioning = true;
  g_transition_to_won = g_run_mode == RunMode::Levels &&
                        g_level + 1 >= kLevelCount;
  g_transition_phase = TransitionPhase::Out;
  g_transition_started = millis();
  g_map = false;
  return true;
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
    save_best_score();
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
    if (g_run_mode == RunMode::Infinite) add_score(50);
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
        e.attack_until = now + 180;
      }
    } else if (hunt) {
      const float dx = (see ? g_px : e.lx) - e.x;
      const float dy = (see ? g_py : e.ly) - e.y;
      step_actor(i, dx, dy, sqrtf(dx * dx + dy * dy), speed, dt);
      if (see && pdist < 0.75f && now >= e.next_act) {
        hurt(15);
        e.next_act = now + 700;
        e.attack_until = now + 180;
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
  const bool shotgun_shot = g_shotgun && g_ammo >= 2;
  if (shotgun_shot) {
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
  g_kick = shotgun_shot ? 9 : 6;
  g_flash_until = now + 80;
}

}  // namespace

void game_setup() {
  check_art();
  g_mode = Mode::Title;
  g_rng = millis() ^ micros() ^ 0xA341316Cu;
  if (g_rng == 0) g_rng = 1;
  Preferences prefs;
  if (prefs.begin("minidoom", true)) {
    g_best_score = prefs.getUInt("inf_best", 0);
    prefs.end();
  }
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

  if (g_transitioning) {
    if (now - g_transition_started >= 320u) {
      if (g_transition_phase == TransitionPhase::Out) {
        if (g_transition_to_won) {
          g_mode = Mode::Won;
        } else if (g_run_mode == RunMode::Infinite) {
          add_score(100);
          save_best_score();
          ++g_infinite_map;
          generate_infinite_map();
          char label[16];
          snprintf(label, sizeof label, "MAP %lu", (unsigned long)g_infinite_map);
          say(label, 1300);
        } else {
          load_level((uint8_t)(g_level + 1));
          say(kLevels[g_level].name, 1300);
        }
        g_transition_phase = TransitionPhase::In;
        g_transition_started = now;
      } else {
        g_transitioning = false;
      }
    }
    return;
  }

  if (g_mode == Mode::Title) {
    if (input.use_edge || input.fire_edge) g_mode = Mode::Menu;
    return;
  }
  if (g_mode == Mode::Menu) {
    if (now >= g_menu_repeat_at && abs(input.axis_y) > 550) {
      g_menu_choice = input.axis_y < 0 ? 0 : 1;
      g_menu_repeat_at = now + 220;
    }
    if (input.use_edge) {
      g_mode = Mode::Title;
      return;
    }
    if (input.fire_edge) {
      if (g_menu_choice == 0) begin_level();
      else begin_infinite();
    }
    return;
  }
  if (g_mode == Mode::Dead || g_mode == Mode::Won) {
    if (input.fire_edge || input.use_edge) g_mode = Mode::Menu;
    return;
  }
  if (g_mode != Mode::Play) {
    if (input.fire_edge || input.use_edge) {
      g_mode = Mode::Menu;
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
  const int old_cell_x = (int)floorf(g_px);
  const int old_cell_y = (int)floorf(g_py);
  try_move(g_dir_x * step, g_dir_y * step);
  update_gate(now);
  if (use_gate_if_entered(old_cell_x, old_cell_y)) {
    return;
  }
  if (fabsf(forward) > 0.12f) {
    g_bob_phase += dt * 12.0f * fabsf(forward);
  }
  g_bob = (int)(sinf(g_bob_phase) * 2.0f);

  take_pickups();
  if (g_exit_x >= 0 && (int)floorf(g_px) == g_exit_x &&
      (int)floorf(g_py) == g_exit_y) {
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
    g_map = true;
  }
}

void game_draw() {
  if (g_mode == Mode::Title) {
    draw_title();
    return;
  }
  if (g_mode == Mode::Menu) {
    draw_menu();
    return;
  }
  if (g_map && g_mode == Mode::Play) {
    draw_map();
    return;
  }
  draw_world();
}
