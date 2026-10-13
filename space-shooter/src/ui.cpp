#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "oled.h"
#include "sprites.h"

namespace {

int g_ox = 0;
int g_oy = 0;

void plot(int x, int y) {
  x += g_ox;
  y += g_oy;
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) {
    return;
  }
  oled.drawPixel(static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
}

void fill(int x, int y, int w, int h) {
  x += g_ox;
  y += g_oy;
  if (w <= 0 || h <= 0 || x >= kScreenW || y >= kScreenH) {
    return;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  if (x + w > kScreenW) {
    w = kScreenW - x;
  }
  if (y + h > kScreenH) {
    h = kScreenH - y;
  }
  oled.drawBox(static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w),
               static_cast<u8g2_uint_t>(h));
}

void blit(int x, int y, int w, int h, const uint8_t *bmp) {
  const int span = (w + 7) / 8;
  for (int row = 0; row < h; ++row) {
    const int dy = y + row;
    if (dy < 0 || dy >= kScreenH) {
      continue;
    }
    const uint8_t *bits = bmp + row * span;
    for (int col = 0; col < w; ++col) {
      if ((bits[col >> 3] & (1 << (col & 7))) != 0) {
        plot(x + col, dy);
      }
    }
  }
}

const uint8_t *glyph_rows(char c) {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 32);
  }
  if (c < kFontFirst || c > kFontLast) {
    c = ' ';
  }
  return kFont + (c - kFontFirst) * kFontH;
}

void draw_glyph(int x, int y, char c, int scale) {
  const uint8_t *rows = glyph_rows(c);
  for (int row = 0; row < kFontH; ++row) {
    const uint8_t bits = rows[row];
    for (int col = 0; col < kFontW; ++col) {
      if ((bits & (1 << col)) == 0) {
        continue;
      }
      if (scale == 1) {
        plot(x + col, y + row);
      } else {
        fill(x + col * scale, y + row * scale, scale, scale);
      }
    }
  }
}

int text_width(const char *text, int scale) {
  const int n = static_cast<int>(strlen(text));
  if (n <= 0) {
    return 0;
  }
  return (n - 1) * (kFontW * scale + 1) + kFontW * scale;
}

void draw_text(int x, int y, const char *text, int scale) {
  while (*text != '\0') {
    draw_glyph(x, y, *text, scale);
    if (text[1] != '\0') {
      x += kFontW * scale + 1;
    }
    ++text;
  }
}

void draw_centered(int y, const char *text, int scale) {
  const int width = text_width(text, scale);
  int x = (kScreenW - width) / 2;
  if (x < 0) {
    x = 0;
  }
  draw_text(x, y, text, scale);
}

void format_u(char *dst, unsigned value, int digits) {
  for (int i = digits - 1; i >= 0; --i) {
    dst[i] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }
  dst[digits] = '\0';
}

void draw_star(const Star *star, const Game *game) {
  if (star->kind > 0 && ((game->frame + star->twinkle) % 11) == 0) {
    return;
  }
  const int layer = static_cast<int>(star->kind) + 1;
  const int sy = static_cast<int>(star->y);
  const int dx = static_cast<int>(game->lean_x) * layer / 120 +
                 static_cast<int>(game->lean_x) * (sy - kScreenH / 2) * layer / 8192;
  const int dy = static_cast<int>(game->lean_y) * layer / 160;
  int x = static_cast<int>(star->x) + dx;
  int y = sy + dy;
  x %= kScreenW;
  if (x < 0) {
    x += kScreenW;
  }
  y %= kScreenH;
  if (y < 0) {
    y += kScreenH;
  }
  plot(x, y);
  if (star->kind == 0) {
    return;
  }
  plot(x - 1, y);
  plot(x + 1, y);
  plot(x, y - 1);
  plot(x, y + 1);
  if (star->kind == 2 && ((game->frame + star->twinkle) % 5) != 0) {
    plot(x - 1, y - 1);
    plot(x + 1, y - 1);
    plot(x - 1, y + 1);
    plot(x + 1, y + 1);
  }
}

void draw_stars(const Game *game) {
  for (int i = 0; i < kMaxStars; ++i) {
    draw_star(&game->stars[i], game);
  }
}

void draw_ghosts(const Game *game) {
  for (int i = 0; i < kMaxGhosts; ++i) {
    const Ghost *ghost = &game->ghosts[i];
    if (ghost->life == 0) {
      continue;
    }
    const int x = ghost->x;
    const int y = ghost->y;
    const bool on = ghost->life >= 3 || (ghost->life == 2 && ((x + y) & 1) == 0) ||
                    (ghost->life == 1 && ((x + y) & 3) == 0);
    if (on) {
      plot(x, y);
    }
  }
}

void draw_chevron_row(int y, char ch) {
  for (int x = 0; x <= kScreenW - kFontW; x += kFontW) {
    draw_glyph(x, y, ch, 1);
  }
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

void draw_bits(const Game *game) {
  for (int i = 0; i < kMaxBits; ++i) {
    if (!game->bits[i].alive) {
      continue;
    }
    const int x = game->bits[i].x >> 4;
    const int y = game->bits[i].y >> 4;
    plot(x, y);
    if (game->bits[i].life > 8) {
      plot(x + 1, y);
      plot(x, y + 1);
    }
  }
}

void draw_saucer_at(int x, int y, uint16_t frame) {
  const uint8_t *bmp = ((frame / 12) & 1) == 0 ? kSaucer : kSaucerB;
  blit(x, y, kSaucerW, kSaucerH, bmp);
}

void draw_ship(const Game *game) {
  if (game->iframes > 0 && ((game->frame / 2) & 1) == 0) {
    return;
  }
  const int x = game->px >> 4;
  const int y = game->py >> 4;
  const bool thrust = ((game->frame / 4) & 1) == 0;
  const uint8_t *bmp = kShip;
  if (game->bank < 0) {
    bmp = thrust ? kShipL : kShipLB;
  } else if (game->bank > 0) {
    bmp = thrust ? kShipR : kShipRB;
  } else {
    bmp = thrust ? kShip : kShipB;
  }
  blit(x, y, kShipW, kShipH, bmp);
  if (!game->shield) {
    return;
  }
  const int x0 = x - 2;
  const int y0 = y - 2;
  const int x1 = x + kShipW + 1;
  const int y1 = y + kShipH + 1;
  for (int px = x0; px <= x1; px += 2) {
    plot(px, y0);
    plot(px, y1);
  }
  for (int py = y0; py <= y1; py += 2) {
    plot(x0, py);
    plot(x1, py);
  }
}

void draw_hud(const Game *game) {
  char score[6];
  char wave[3];
  char lives[2];
  format_u(score, game->score > 99999 ? 99999 : game->score, 5);
  format_u(wave, game->wave > 99 ? 99 : game->wave, 2);
  format_u(lives, game->lives > 9 ? 9 : game->lives, 1);
  oled.setDrawColor(0);
  fill(0, 0, kScreenW, kHudH);
  oled.setDrawColor(1);
  draw_text(0, kHudTextY, score, 1);
  draw_text(kWaveX, kHudTextY, wave, 1);
  blit(kHeartX, kHudTextY, kHeartW, kHeartH, kHeart);
  draw_text(kLivesX, kHudTextY, lives, 1);
  for (int x = 0; x < kScreenW; x += 2) {
    plot(x, kHudLineY);
  }
}

void draw_play(const Game *game) {
  draw_stars(game);
  for (int i = 0; i < kMaxRocks; ++i) {
    if (!game->rocks[i].alive) {
      continue;
    }
    const uint8_t *bmp = ((game->frame / 8) & 1) == 0 ? kRock : kRockB;
    blit(game->rocks[i].x >> 4, game->rocks[i].y >> 4, kRockW, kRockH, bmp);
  }
  if (game->power_on) {
    const uint8_t *bmp = game->power_kind == 0 ? kSpread : kShieldIcon;
    blit(game->power_x >> 4, game->power_y >> 4, kPowerW, kPowerH, bmp);
  }
  for (int i = 0; i < kMaxInvaders; ++i) {
    if (game->inv[i].state == InvState::Empty) {
      continue;
    }
    int x = 0;
    int y = 0;
    if (game->inv[i].state == InvState::Formed) {
      x = (game->form_x >> 4) + game->inv[i].col * kStrideX;
      y = (game->form_y >> 4) + game->inv[i].row * kStrideY;
    } else {
      x = game->inv[i].x >> 4;
      y = game->inv[i].y >> 4;
    }
    draw_saucer_at(x, y, game->frame);
  }
  if (game->boss_on) {
    const uint8_t *bmp = ((game->frame / 14) & 1) == 0 ? kBoss : kBossB;
    blit(game->boss_x >> 4, game->boss_y >> 4, kBossW, kBossH, bmp);
    if (game->boss_max > 0) {
      const int width = 36;
      const int filled = game->boss_hp * width / game->boss_max;
      const int x = (kScreenW - width) / 2;
      const int y = kHudH + 1;
      for (int i = 0; i < width; ++i) {
        plot(x + i, y + 2);
        if (i < filled) {
          plot(x + i, y);
          plot(x + i, y + 1);
        }
      }
    }
  }
  draw_ghosts(game);
  for (int i = 0; i < kMaxPlayerShots; ++i) {
    if (game->player_shots[i].alive) {
      fill(game->player_shots[i].x >> 4, game->player_shots[i].y >> 4, 1, 4);
    }
  }
  for (int i = 0; i < kMaxEnemyShots; ++i) {
    if (game->enemy_shots[i].alive) {
      fill(game->enemy_shots[i].x >> 4, game->enemy_shots[i].y >> 4, 2, 3);
    }
  }
  draw_bits(game);
  draw_ship(game);
  if (game->wave_banner > 0) {
    char line[12];
    if (game->wave % 5 == 0) {
      snprintf(line, sizeof(line), "BOSS");
    } else {
      snprintf(line, sizeof(line), "WAVE %u", static_cast<unsigned>(game->wave));
    }
    const int width = text_width(line, 1);
    const int box_w = width + 6;
    const int box_h = kFontH + 4;
    const int travel = kScreenW + box_w;
    const int t = kWaveWipe - game->wave_banner;
    const int x = -box_w + (kWaveWipe <= 1 ? 0 : t * travel / (kWaveWipe - 1));
    const int y = kWaveBannerY;
    oled.setDrawColor(0);
    fill(x, y, box_w, box_h);
    oled.setDrawColor(1);
    for (int i = 0; i < box_w; ++i) {
      plot(x + i, y);
      plot(x + i, y + box_h - 1);
    }
    for (int i = 0; i < box_h; ++i) {
      plot(x, y + i);
      plot(x + box_w - 1, y + i);
    }
    draw_text(x + 3, y + 2, line, 1);
  }
  draw_hud(game);
}

void draw_menu(const Game *game) {
  draw_stars(game);
  draw_centered(kTitleY, "SPACE", 2);
  draw_centered(kTitle2Y, "SHOOTER", 2);
  const int bob = ((game->frame / 4) & 7) < 4 ? 0 : 2;
  draw_saucer_at((kScreenW - kSaucerW) / 2, kMenuSaucerY + bob, game->frame);
  const int item_y[4] = {kItemStartY, kItemDiffY, kItemScoresY, kItemSoundY};
  const char *items[4] = {"START", "DIFFICULTY", "HIGH SCORES", nullptr};
  char sound_line[12];
  snprintf(sound_line, sizeof(sound_line), game->sound_on ? "SOUND ON" : "SOUND OFF");
  items[3] = sound_line;
  for (int i = 0; i < 4; ++i) {
    if (i == game->menu_i) {
      draw_glyph(0, item_y[i], '>', 1);
    }
    draw_text(kMenuTextX, item_y[i], items[i], 1);
  }
  const bool blink = game->menu_i == 1 && ((game->frame / 8) & 1) == 0;
  if (!blink) {
    draw_text(12, kItemDiffValueY, diff_name(game->diff), 1);
  }
}

void draw_scores(const Game *game) {
  draw_stars(game);
  draw_centered(18, "HIGH SCORES", 1);
  for (int i = 0; i < 3; ++i) {
    char line[12];
    char num[6];
    format_u(num, game->top[i] > 99999 ? 99999 : game->top[i], 5);
    snprintf(line, sizeof(line), "%d %s", i + 1, num);
    draw_centered(40 + i * 14, line, 1);
  }
}

void draw_pause(const Game *game) {
  oled.setDrawColor(0);
  fill(kPauseBoxX, kPauseBoxY, kPauseBoxW, kPauseBoxH);
  oled.setDrawColor(1);
  oled.drawFrame(static_cast<u8g2_uint_t>(kPauseBoxX + g_ox), static_cast<u8g2_uint_t>(kPauseBoxY + g_oy),
                 static_cast<u8g2_uint_t>(kPauseBoxW), static_cast<u8g2_uint_t>(kPauseBoxH));
  draw_centered(kPauseTitleY, "PAUSED", 1);
  const char *items[3] = {"RESUME", "RESTART", "QUIT"};
  for (int i = 0; i < 3; ++i) {
    const int y = kPauseItemY0 + i * kPauseItemDy;
    if (i == game->pause_i) {
      draw_glyph(kPauseTextX - 6, y, '>', 1);
    }
    draw_text(kPauseTextX, y, items[i], 1);
  }
}

void draw_over(const Game *game) {
  draw_stars(game);
  const int bob = ((game->frame / 4) & 7) < 4 ? 0 : 2;
  draw_saucer_at((kScreenW - kSaucerW) / 2, kOverSaucerY + bob, game->frame);
  char hi[10];
  char sc[10];
  char num[6];
  format_u(num, game->top[0] > 99999 ? 99999 : game->top[0], 5);
  snprintf(hi, sizeof(hi), "HI %s", num);
  format_u(num, game->score > 99999 ? 99999 : game->score, 5);
  snprintf(sc, sizeof(sc), "SC %s", num);
  const bool blink_score = game->rank == 0 && ((game->frame / 8) & 1) == 0;
  draw_centered(kOverHiY, hi, 1);
  if (!blink_score) {
    draw_centered(kOverScoreY, sc, 1);
  }
  draw_chevron_row(kOverChevronY, '>');
  draw_centered(kOverTitleY, "GAME OVER", 1);
  draw_chevron_row(kOverChevron2Y, '<');
}

}  // namespace

void ui_draw(const Game *game) {
  g_ox = game->shake_x;
  g_oy = game->shake_y;
  oled.clearBuffer();
  oled.setDrawColor(1);
  switch (game->screen) {
    case Screen::Menu:
      draw_menu(game);
      break;
    case Screen::Scores:
      draw_scores(game);
      break;
    case Screen::Playing:
      draw_play(game);
      break;
    case Screen::Paused:
      draw_play(game);
      draw_pause(game);
      break;
    case Screen::GameOver:
      draw_over(game);
      break;
  }
  if (game->flash > 0) {
    oled.setDrawColor(2);
    oled.drawBox(0, 0, static_cast<u8g2_uint_t>(kScreenW), static_cast<u8g2_uint_t>(kScreenH));
    oled.setDrawColor(1);
  }
  oled.sendBuffer();
}
