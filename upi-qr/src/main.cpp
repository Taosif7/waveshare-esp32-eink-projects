#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "eink_status.h"

#ifndef WIFI_SSID
#error "WIFI_SSID missing — copy repo-root .env.example to .env and rebuild"
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS missing — copy repo-root .env.example to .env and rebuild"
#endif

static WebServer server(80);
static char g_web_url[40] = "";

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>UPI QR → e-paper</title>
  <style>
    :root { color-scheme: light; }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: Georgia, "Times New Roman", serif;
      background: linear-gradient(160deg, #f3efe6 0%, #d9e2d4 50%, #b7c7b0 100%);
      color: #1a2a22;
    }
    main {
      max-width: 26rem;
      margin: 0 auto;
      padding: 2rem 1.25rem 3rem;
    }
    h1 { font-size: 1.75rem; margin: 0 0 0.35rem; }
    .lede { margin: 0 0 1.5rem; opacity: 0.8; line-height: 1.4; }
    label {
      display: block;
      font-size: 0.85rem;
      margin: 0.9rem 0 0.3rem;
      letter-spacing: 0.02em;
    }
    label .req { color: #8a3b2b; }
    input {
      width: 100%;
      padding: 0.65rem 0.7rem;
      border: 1px solid #6f7f72;
      border-radius: 0;
      background: rgba(255,255,255,0.7);
      font: inherit;
      font-size: 1rem;
    }
    input:focus { outline: 2px solid #3d5a45; outline-offset: 1px; }
    .hint { font-size: 0.75rem; opacity: 0.7; margin: 0.25rem 0 0; }
    button {
      margin-top: 1.4rem;
      width: 100%;
      padding: 0.85rem 1rem;
      border: none;
      background: #1a2a22;
      color: #f3efe6;
      font: inherit;
      font-size: 1.05rem;
      cursor: pointer;
    }
    button:disabled { opacity: 0.55; cursor: wait; }
    #status {
      margin-top: 1rem;
      min-height: 1.4rem;
      font-size: 0.95rem;
    }
    #status.ok { color: #1f5a36; }
    #status.err { color: #8a3b2b; }
    code {
      display: block;
      margin-top: 0.5rem;
      padding: 0.6rem;
      background: rgba(0,0,0,0.06);
      font-size: 0.7rem;
      word-break: break-all;
      font-family: ui-monospace, Menlo, monospace;
    }
  </style>
</head>
<body>
  <main>
    <h1>UPI QR</h1>
    <p class="lede">Personal UPI QR (<code>upi://pay</code>). Only VPA is required. Amount is INR (e.g. <code>100.00</code>), not paise.</p>
    <form id="upi-form">
      <label for="pa">Payee VPA (pa) <span class="req">*</span></label>
      <input id="pa" name="pa" required placeholder="name@oksbi" autocomplete="off">

      <label for="pn">Payee name (pn)</label>
      <input id="pn" name="pn" placeholder="Optional">

      <label for="am">Amount INR (am)</label>
      <input id="am" name="am" inputmode="decimal" placeholder="100.00">
      <p class="hint">Optional. Decimal INR with up to 2 places (not paise). Omit to let payer enter amount.</p>

      <label for="tn">Note (tn)</label>
      <input id="tn" name="tn" maxlength="50" placeholder="Optional, keep short">

      <button type="submit" id="go">Show on e-paper</button>
    </form>
    <p id="status" role="status"></p>
  </main>
  <script>
    const form = document.getElementById('upi-form');
    const status = document.getElementById('status');
    const go = document.getElementById('go');

    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      status.className = '';
      status.textContent = 'Sending… (~2 s refresh)';
      go.disabled = true;
      try {
        const pa = form.pa.value.trim();
        const pn = form.pn.value.trim();
        const am = form.am.value.trim();
        const tn = form.tn.value.trim();
        if (!pa) throw new Error('pa (VPA) is required');
        // JSON body keeps spaces intact; device builds upi:// (spaces → %20; @ in pa raw).
        const res = await fetch('/display', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ pa: pa, pn: pn, am: am, tn: tn })
        });
        const data = await res.json();
        if (!res.ok || !data.ok) {
          throw new Error(data.error || ('HTTP ' + res.status));
        }
        status.className = 'ok';
        status.innerHTML = 'Shown on device.' +
          (data.upi ? '<code>' + data.upi.replace(/</g,'&lt;') + '</code>' : '');
      } catch (err) {
        status.className = 'err';
        status.textContent = err.message || String(err);
      } finally {
        go.disabled = false;
      }
    });
  </script>
</body>
</html>
)rawliteral";

/**
 * Percent-encode a UPI query value per NPCI Linking Specs 1.6 / RFC 3986.
 * Spaces must be "%20" (never "+" and never a lone "%").
 * Leave '@' intact when allow_at is set — encoding pa's @ as %40 breaks apps
 * that still treat "%" as space (legacy PSP quirk noted by NPCI).
 */
static String url_encode(const String &value, bool allow_at) {
  static const char kHex[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~' || (allow_at && c == '@')) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

/** Format amount as INR decimal (NPCI: decimal format, not paise). No float printf. */
static bool format_amount_inr(const String &raw, String *out) {
  String trimmed = raw;
  trimmed.trim();
  if (trimmed.isEmpty()) {
    out->clear();
    return true;
  }
  char *end = nullptr;
  const double value = strtod(trimmed.c_str(), &end);
  if (end == trimmed.c_str() || *end != '\0' || value < 0.0 || value > 999999.99) {
    return false;
  }
  // Integer paise avoids newlib-nano `%f` issues on ESP32.
  const long paise = static_cast<long>(value * 100.0 + 0.5);
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld.%02ld", paise / 100, paise % 100);
  *out = buf;
  return true;
}

/**
 * Personal / P2P UPI QR payload.
 *
 *   upi://pay?pa=…[&pn=…][&am=…][&tn=…]&cu=INR
 *
 * Only pa is required (matches real GPay/PhonePe personal QRs). pn/am/tn optional.
 * Omit mc/mode on personal VPAs — they break many apps.
 */
static String build_upi_uri(const String &pa, const String &pn, const String &am, const String &tn) {
  String note = tn;
  if (note.length() > 50) {
    note = note.substring(0, 50);
  }

  String uri = "upi://pay?pa=";
  uri += url_encode(pa, true);
  if (pn.length() > 0) {
    uri += "&pn=";
    uri += url_encode(pn, false);
  }
  if (am.length() > 0) {
    uri += "&am=";
    uri += am;
  }
  if (note.length() > 0) {
    uri += "&tn=";
    uri += url_encode(note, false);
  }
  uri += "&cu=INR";
  return uri;
}

/** Pull a JSON string value for `key` from a flat object body. */
static bool json_string_field(const String &body, const char *key, String *out) {
  const String needle = String("\"") + key + "\":";
  const int key_at = body.indexOf(needle);
  if (key_at < 0) {
    out->clear();
    return false;
  }
  int i = key_at + needle.length();
  while (i < static_cast<int>(body.length()) && (body[i] == ' ' || body[i] == '\t')) {
    ++i;
  }
  if (i >= static_cast<int>(body.length()) || body[i] != '"') {
    out->clear();
    return false;
  }
  ++i;
  String value;
  while (i < static_cast<int>(body.length())) {
    const char c = body[i++];
    if (c == '\\' && i < static_cast<int>(body.length())) {
      value += body[i++];
      continue;
    }
    if (c == '"') {
      *out = value;
      return true;
    }
    value += c;
  }
  out->clear();
  return false;
}

static void handle_root() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_display() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"ok\":false,\"error\":\"POST required\"}");
    return;
  }

  String pa;
  String pn;
  String am_raw;
  String tn;
  const String body = server.arg("plain");

  if (body.length() > 0 && body.indexOf('{') >= 0) {
    json_string_field(body, "pa", &pa);
    json_string_field(body, "pn", &pn);
    json_string_field(body, "am", &am_raw);
    json_string_field(body, "tn", &tn);
  } else {
    pa = server.hasArg("pa") ? server.arg("pa") : "";
    pn = server.hasArg("pn") ? server.arg("pn") : "";
    am_raw = server.hasArg("am") ? server.arg("am") : "";
    tn = server.hasArg("tn") ? server.arg("tn") : "";
  }

  pa.trim();
  pn.trim();
  tn.trim();
  if (pa.isEmpty() || pa.indexOf('@') < 1) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"pa must be a VPA like name@handle\"}");
    return;
  }

  String am;
  if (!format_amount_inr(am_raw, &am)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"am must be INR with up to 2 decimals\"}");
    return;
  }

  const String upi = build_upi_uri(pa, pn, am, tn);
  Serial.printf("UPI: %s\n", upi.c_str());

  if (!eink_show_qr(upi.c_str(), g_web_url)) {
    server.send(500, "application/json",
                "{\"ok\":false,\"error\":\"QR too dense for the display — shorten payee name or note\"}");
    return;
  }

  String json = "{\"ok\":true,\"upi\":\"";
  for (size_t i = 0; i < upi.length(); ++i) {
    const char c = upi[i];
    if (c == '"' || c == '\\') {
      json += '\\';
    }
    json += c;
  }
  json += "\"}";
  server.send(200, "application/json", json);
}

static void handle_not_found() {
  server.send(404, "text/plain", "Not found");
}

static bool connect_wifi(uint32_t timeout_ms) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("upi-qr");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s\n", WIFI_SSID);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeout_ms) {
      Serial.println("WiFi timeout");
      return false;
    }
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("upi-qr boot");

  eink_status_begin();
  eink_status_show("UPI QR", "Connecting", "Wi-Fi...", WIFI_SSID);

  if (!connect_wifi(20000)) {
    eink_status_show("WIFI FAIL", "Check root", ".env SSID/PASS", "Then reflash");
    return;
  }

  snprintf(g_web_url, sizeof(g_web_url), "http://%s", WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, handle_root);
  server.on("/display", HTTP_POST, handle_display);
  server.onNotFound(handle_not_found);
  server.begin();

  Serial.printf("Open %s/\n", g_web_url);
  eink_status_show("UPI QR", "Open in browser", g_web_url, "then send VPA");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
  delay(2);
}
