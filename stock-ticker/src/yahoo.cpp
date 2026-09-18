#include "yahoo.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr uint16_t kCollectMax = 512;
static constexpr uint32_t kStreamStallMs = 8000;

static void url_encode(const char *in, char *out, size_t out_len) {
  size_t j = 0;
  for (size_t i = 0; in[i] != '\0' && j + 4 < out_len; i++) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (isalnum(c) || c == '.' || c == '-' || c == '_') {
      out[j++] = static_cast<char>(c);
    } else {
      snprintf(out + j, 4, "%%%02X", c);
      j += 3;
    }
  }
  out[j] = '\0';
}

static bool is_finite_number(JsonVariantConst v) {
  if (v.isNull() || !v.is<float>()) {
    return false;
  }
  const float n = v.as<float>();
  return isfinite(n);
}

// A day of 1-minute crypto bars runs past 200 kB, so the body is parsed as it
// arrives instead of being buffered. This wrapper refills in blocks, both to
// keep ArduinoJson's per-byte reads cheap and so a momentary gap in the TLS
// stream is not mistaken for the end of the document.
class BufferedBody : public Stream {
 public:
  explicit BufferedBody(NetworkClient &source) : source_(source) {
    setTimeout(kStreamStallMs);
  }

  int available() override {
    return fill() ? static_cast<int>(tail_ - head_) : 0;
  }

  int read() override {
    return fill() ? buf_[head_++] : -1;
  }

  int peek() override {
    return fill() ? buf_[head_] : -1;
  }

  size_t write(uint8_t) override {
    return 0;
  }

  size_t consumed() const {
    return total_;
  }

 private:
  bool fill() {
    if (head_ < tail_) {
      return true;
    }
    head_ = 0;
    tail_ = 0;
    const uint32_t start = millis();
    while ((millis() - start) < kStreamStallMs) {
      const int got = source_.read(buf_, sizeof(buf_));
      if (got > 0) {
        tail_ = static_cast<size_t>(got);
        total_ += static_cast<size_t>(got);
        return true;
      }
      if (!source_.connected() && source_.available() <= 0) {
        return false;
      }
      delay(2);
    }
    return false;
  }

  NetworkClient &source_;
  uint8_t buf_[1024];
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t total_ = 0;
};

static void build_filter(JsonDocument &filter) {
  filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
  filter["chart"]["result"][0]["meta"]["previousClose"] = true;
  filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
  filter["chart"]["result"][0]["meta"]["regularMarketChangePercent"] = true;
  filter["chart"]["result"][0]["timestamp"] = true;
  filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
}

static void downsample(const float *in, uint16_t n, float *out, uint16_t *out_n) {
  if (n <= kMaxPoints) {
    memcpy(out, in, n * sizeof(float));
    *out_n = n;
    return;
  }
  for (uint16_t i = 0; i < kMaxPoints; i++) {
    const uint16_t src = static_cast<uint16_t>((static_cast<uint32_t>(i) * (n - 1)) / (kMaxPoints - 1));
    out[i] = in[src];
  }
  *out_n = kMaxPoints;
}

static bool parse_chart(const JsonDocument &doc, const Stock &stock, uint32_t range_seconds,
                        Quote *out) {
  JsonVariantConst result = doc["chart"]["result"][0];
  if (result.isNull()) {
    Serial.printf("yahoo: empty result for %s\n", stock.symbol);
    return false;
  }

  JsonVariantConst meta = result["meta"];
  float price = 0;
  if (is_finite_number(meta["regularMarketPrice"])) {
    price = meta["regularMarketPrice"].as<float>();
  }

  float prev = 0;
  if (is_finite_number(meta["previousClose"])) {
    prev = meta["previousClose"].as<float>();
  } else if (is_finite_number(meta["chartPreviousClose"])) {
    prev = meta["chartPreviousClose"].as<float>();
  }

  float change_pct = 0;
  if (is_finite_number(meta["regularMarketChangePercent"])) {
    change_pct = meta["regularMarketChangePercent"].as<float>();
  } else if (prev != 0.0f && price != 0.0f) {
    change_pct = (price - prev) * 100.0f / prev;
  }

  JsonArrayConst closes = result["indicators"]["quote"][0]["close"].as<JsonArrayConst>();
  JsonArrayConst stamps = result["timestamp"].as<JsonArrayConst>();
  const size_t n_close = closes.size();
  const size_t n_ts = stamps.size();
  const size_t n = n_close < n_ts ? n_close : n_ts;

  uint32_t window = range_seconds;
  if (window < 60) {
    window = 60;
  }
  uint32_t last_ts = 0;
  if (n_ts > 0 && stamps[n_ts - 1].is<uint32_t>()) {
    last_ts = stamps[n_ts - 1].as<uint32_t>();
  }
  const uint32_t cutoff = last_ts > window ? last_ts - window : 0;

  float collected[kCollectMax];
  uint16_t count = 0;
  for (size_t i = 0; i < n; i++) {
    if (cutoff != 0 && stamps[i].is<uint32_t>()) {
      const uint32_t ts = stamps[i].as<uint32_t>();
      if (ts < cutoff) {
        continue;
      }
    }
    if (!is_finite_number(closes[i])) {
      continue;
    }
    if (count < kCollectMax) {
      collected[count++] = closes[i].as<float>();
    } else {
      memmove(collected, collected + 1, (kCollectMax - 1) * sizeof(float));
      collected[kCollectMax - 1] = closes[i].as<float>();
    }
  }

  if (count == 0) {
    for (size_t i = 0; i < n_close; i++) {
      if (!is_finite_number(closes[i])) {
        continue;
      }
      if (count < kCollectMax) {
        collected[count++] = closes[i].as<float>();
      } else {
        memmove(collected, collected + 1, (kCollectMax - 1) * sizeof(float));
        collected[kCollectMax - 1] = closes[i].as<float>();
      }
    }
  }

  if (count == 0 && price == 0.0f) {
    return false;
  }

  float tmp[kMaxPoints];
  uint16_t drawn = 0;
  downsample(collected, count, tmp, &drawn);

  if (price == 0.0f && drawn > 0) {
    price = tmp[drawn - 1];
  }
  if (drawn > 0 && price != 0.0f) {
    tmp[drawn - 1] = price;
  }
  if (prev != 0.0f && price != 0.0f && change_pct == 0.0f) {
    change_pct = (price - prev) * 100.0f / prev;
  }

  out->valid = true;
  out->price = price;
  out->prev_close = prev;
  out->change_pct = change_pct;
  out->n_points = drawn;
  memcpy(out->points, tmp, drawn * sizeof(float));
  out->fetched_ms = millis();
  Serial.printf("yahoo: %s range=%us price=%.2f pct=%.2f bars=%u->%u\n", stock.symbol, window, price,
                change_pct, count, drawn);
  return true;
}

static bool fetch_once(const Stock &stock, const RangeOption &range, Quote *out) {
  char encoded[40];
  url_encode(stock.symbol, encoded, sizeof(encoded));

  char url[192];
  snprintf(url, sizeof(url),
           "https://query1.finance.yahoo.com/v8/finance/chart/%s"
           "?range=%s&interval=%s&includePrePost=false",
           encoded, range.yahoo_range, range.yahoo_interval);

  NetworkClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(20);

  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120.0.0.0");

  if (!http.begin(client, url)) {
    Serial.println("yahoo: begin failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("yahoo: HTTP %d for %s\n", code, stock.symbol);
    http.end();
    return false;
  }

  NetworkClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("yahoo: no stream");
    http.end();
    return false;
  }

  JsonDocument filter;
  build_filter(filter);

  BufferedBody body(*stream);
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  const size_t consumed = body.consumed();
  http.end();

  if (err) {
    Serial.printf("yahoo: json %s after %u bytes heap=%u\n", err.c_str(),
                  static_cast<unsigned>(consumed), static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }
  Serial.printf("yahoo: read %u bytes heap=%u\n", static_cast<unsigned>(consumed),
                static_cast<unsigned>(ESP.getFreeHeap()));

  return parse_chart(doc, stock, range.seconds, out);
}

bool yahoo_fetch_chart(const Stock &stock, Quote *out, const RangeOption &range) {
  Serial.printf("yahoo: fetch %s %s/%s\n", stock.symbol, range.yahoo_range, range.yahoo_interval);
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (fetch_once(stock, range, out)) {
      return true;
    }
    Serial.printf("yahoo: attempt %d failed\n", attempt);
    delay(400);
  }
  return false;
}
