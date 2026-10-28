#pragma once

#include <stdint.h>

static constexpr int kMaxSessions = 8;
static constexpr int kMaxOptions = 8;

struct UiModel {
  bool link;
  int battery;
  int more;
  bool question;
  const char *header;
  const char *prompt;
  char options[kMaxOptions][40];
  int option_count;
  char marks[kMaxSessions];
  char names[kMaxSessions][48];
  int session_count;
  int highlight;
};

void ui_draw(const UiModel *model, bool full);
