#include "lyrics.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <ctype.h>
#include <string.h>

namespace {

constexpr int kTextCap = 72;

bool parse_time(const char *begin, const char *end, uint32_t *time_ms) {
  const char *p = begin;
  if (p >= end || !isdigit(static_cast<unsigned char>(*p))) {
    return false;
  }
  int minutes = 0;
  while (p < end && isdigit(static_cast<unsigned char>(*p))) {
    minutes = minutes * 10 + (*p - '0');
    ++p;
  }
  if (p >= end || *p != ':') {
    return false;
  }
  ++p;
  if (p + 2 > end || !isdigit(static_cast<unsigned char>(p[0])) ||
      !isdigit(static_cast<unsigned char>(p[1]))) {
    return false;
  }
  const int seconds = (p[0] - '0') * 10 + (p[1] - '0');
  p += 2;
  if (seconds > 59 || p >= end || *p != '.') {
    return false;
  }
  ++p;
  if (p + 2 != end || !isdigit(static_cast<unsigned char>(p[0])) ||
      !isdigit(static_cast<unsigned char>(p[1]))) {
    return false;
  }
  const int centis = (p[0] - '0') * 10 + (p[1] - '0');
  *time_ms = static_cast<uint32_t>(minutes * 60 + seconds) * 1000u +
             static_cast<uint32_t>(centis) * 10u;
  return true;
}

bool parse_cue(const char *line, LyricLine *out) {
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  if (*line == '\0' || *line == '#') {
    return false;
  }
  const char *bar1 = strchr(line, '|');
  if (bar1 == nullptr) {
    return false;
  }
  const char *bar2 = strchr(bar1 + 1, '|');
  if (bar2 == nullptr) {
    return false;
  }
  uint32_t time_ms = 0;
  if (!parse_time(line, bar1, &time_ms)) {
    return false;
  }
  const char *frame_text = bar1 + 1;
  if (frame_text == bar2 || !isdigit(static_cast<unsigned char>(*frame_text))) {
    return false;
  }
  int frame = 0;
  while (frame_text < bar2 && isdigit(static_cast<unsigned char>(*frame_text))) {
    frame = frame * 10 + (*frame_text - '0');
    ++frame_text;
  }
  if (frame < 1) {
    frame = 1;
  }
  if (frame > 16) {
    frame = 16;
  }

  const char *text = bar2 + 1;
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  size_t length = strlen(text);
  while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t')) {
    --length;
  }
  if (length >= static_cast<size_t>(kTextCap)) {
    length = kTextCap - 1;
  }
  memcpy(out->text, text, length);
  out->text[length] = '\0';
  out->time_ms = time_ms;
  out->frame = static_cast<uint8_t>(frame);
  return true;
}

void sort_cues(LyricScript *script) {
  for (int i = 1; i < script->count; ++i) {
    LyricLine key = script->lines[i];
    int j = i;
    while (j > 0 && script->lines[j - 1].time_ms > key.time_ms) {
      script->lines[j] = script->lines[j - 1];
      --j;
    }
    script->lines[j] = key;
  }
}

bool read_line(File &file, char *buf, int cap) {
  int n = 0;
  bool any = false;
  while (file.available()) {
    any = true;
    const int c = file.read();
    if (c < 0 || c == '\n') {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (n < cap - 1) {
      buf[n++] = static_cast<char>(c);
    }
  }
  buf[n] = '\0';
  return any;
}

}  // namespace

bool lyrics_load(LyricScript *script) {
  script->count = 0;
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS did not mount.");
    return false;
  }
  File file = LittleFS.open("/lyrics.txt", "r");
  if (!file) {
    Serial.println("No /lyrics.txt. Copy lyrics.example.txt to data/lyrics.txt, then pio run -t uploadfs.");
    return false;
  }

  char line[128];
  int skipped = 0;
  while (read_line(file, line, sizeof line)) {
    if (script->count >= kLyricCap) {
      Serial.println("Lyric file truncated at 48 cues.");
      break;
    }
    LyricLine cue = {};
    if (!parse_cue(line, &cue)) {
      if (line[0] != '\0' && line[0] != '#') {
        ++skipped;
      }
      continue;
    }
    script->lines[script->count] = cue;
    ++script->count;
  }
  file.close();
  sort_cues(script);
  Serial.printf("Loaded %d cues from /lyrics.txt", script->count);
  if (skipped > 0) {
    Serial.printf(" (%d lines skipped)", skipped);
  }
  Serial.println(".");
  return script->count > 0;
}
