#include "app.h"

#include <stdio.h>
#include <string.h>

#include <Arduino.h>

#include "ble.h"
#include "sound.h"
#include "ui.h"

namespace {
struct Frame {
  int gen;
  char marks[kMaxSessions];
  char names[kMaxSessions][48];
  int session_count;
  char header[24];
  char prompt[180];
  char options[kMaxOptions][40];
  int option_count;
  bool has_question;
};

Frame live = {};
Frame build = {};
bool saw_at = false;
int silenced_gen = -1;
int highlight = 0;
bool chiming = false;
uint32_t next_chiming_ms = 0;
int battery = -1;
bool dirty = true;
bool full_paint = true;
int partials = 0;
bool was_question = false;

void copy_field(char *dst, size_t len, const char *src) {
  strncpy(dst, src, len - 1);
  dst[len - 1] = '\0';
}

int waiting_count(const Frame *frame) {
  int n = 0;
  for (int i = 0; i < frame->session_count; i++) {
    if (frame->marks[i] == 'Q') {
      n++;
    }
  }
  if (frame->has_question && n > 0) {
    n -= 1;
  }
  return n < 0 ? 0 : n;
}

bool same_question(const Frame *a, const Frame *b) {
  if (!a->has_question || !b->has_question) {
    return false;
  }
  if (strcmp(a->prompt, b->prompt) != 0 || a->option_count != b->option_count) {
    return false;
  }
  for (int i = 0; i < a->option_count; i++) {
    if (strcmp(a->options[i], b->options[i]) != 0) {
      return false;
    }
  }
  return true;
}

int item_count(void);

void commit_frame(void) {
  const bool keep = same_question(&live, &build);
  live = build;
  if (!keep) {
    highlight = 0;
  }
  const int n = item_count();
  if (n <= 0) {
    highlight = 0;
  } else if (highlight >= n) {
    highlight = n - 1;
  }
  if (!live.has_question || live.gen == silenced_gen) {
    chiming = false;
    sound_stop();
  }
  dirty = true;
  full_paint = true;
  memset(&build, 0, sizeof(build));
  saw_at = false;
}

void add_session(char mark, const char *name) {
  if (build.session_count >= kMaxSessions) {
    return;
  }
  const int i = build.session_count++;
  build.marks[i] = mark;
  copy_field(build.names[i], sizeof(build.names[i]), name);
}

void add_option(const char *label) {
  if (build.option_count >= kMaxOptions) {
    return;
  }
  copy_field(build.options[build.option_count], sizeof(build.options[0]), label);
  build.option_count++;
  build.has_question = true;
}

int item_count(void) {
  if (live.has_question && live.gen != silenced_gen) {
    return live.option_count;
  }
  return live.session_count;
}

void move_highlight(void) {
  const int n = item_count();
  if (n <= 0) {
    return;
  }
  highlight = (highlight + 1) % n;
  dirty = true;
}

void paint(void) {
  UiModel model = {};
  model.link = ble_connected();
  model.battery = battery;
  model.more = waiting_count(&live);
  model.question = live.has_question && live.gen != silenced_gen;
  model.header = live.header;
  model.prompt = live.prompt;
  model.option_count = live.option_count;
  for (int i = 0; i < live.option_count; i++) {
    copy_field(model.options[i], sizeof(model.options[i]), live.options[i]);
  }
  model.session_count = live.session_count;
  for (int i = 0; i < live.session_count; i++) {
    model.marks[i] = live.marks[i];
    copy_field(model.names[i], sizeof(model.names[i]), live.names[i]);
  }
  model.highlight = highlight;
  const bool full = full_paint || model.question != was_question || partials >= 30;
  ui_draw(&model, full);
  was_question = model.question;
  partials = full ? 0 : partials + 1;
  full_paint = false;
  dirty = false;
}
}  // namespace

void app_init(void) {
  dirty = true;
  full_paint = true;
}

void app_on_line(const char *line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }
  if (line[0] == '@') {
    memset(&build, 0, sizeof(build));
    build.gen = atoi(line + 1);
    saw_at = true;
    return;
  }
  if (strcmp(line, "!") == 0) {
    if (!saw_at) {
      build.gen = live.gen;
    }
    commit_frame();
    return;
  }
  if (strncmp(line, "BEEP ", 5) == 0) {
    const char kind = line[5];
    if (kind == 'Q') {
      chiming = true;
      next_chiming_ms = millis() + 2500;
      sound_play(Tone::Question);
    } else if (kind == 'E') {
      sound_play(Tone::Error);
    } else if (kind == 'D') {
      sound_play(Tone::Done);
    }
    return;
  }
  if (strncmp(line, "S ", 2) == 0 && line[2] != '\0') {
    const char *name = line[3] == ' ' ? line + 4 : line + 2;
    add_session(line[2], name);
    return;
  }
  if (strncmp(line, "T ", 2) == 0) {
    copy_field(build.header, sizeof(build.header), line + 2);
    return;
  }
  if (strncmp(line, "Q ", 2) == 0) {
    copy_field(build.prompt, sizeof(build.prompt), line + 2);
    build.has_question = true;
    return;
  }
  if (strncmp(line, "O ", 2) == 0) {
    add_option(line + 2);
  }
}

void app_on_button(BtnEvent event) {
  const bool on_question = live.has_question && live.gen != silenced_gen;
  if (event == BtnEvent::Next) {
    move_highlight();
    return;
  }
  if (event == BtnEvent::Confirm) {
    if (!on_question || live.option_count <= 0) {
      return;
    }
    char msg[16];
    snprintf(msg, sizeof(msg), "P %d", highlight);
    ble_send_line(msg);
    chiming = false;
    sound_stop();
    return;
  }
  if (event == BtnEvent::Dismiss && on_question) {
    silenced_gen = live.gen;
    chiming = false;
    sound_stop();
    ble_send_line("K");
    dirty = true;
    full_paint = true;
  }
}

void app_set_battery(int percent) {
  if (percent != battery) {
    battery = percent;
    dirty = true;
  }
}

void app_tick(void) {
  if (chiming && static_cast<int32_t>(millis() - next_chiming_ms) >= 0) {
    sound_play(Tone::Question);
    next_chiming_ms = millis() + 2500;
  }
  static bool last_link = false;
  const bool link = ble_connected();
  if (link != last_link) {
    last_link = link;
    dirty = true;
  }
  if (dirty) {
    paint();
  }
}
