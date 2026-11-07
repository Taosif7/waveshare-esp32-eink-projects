#include "web.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "web_assets.h"

#ifndef WIFI_SSID
#error "WIFI_SSID missing — check workspace-root .env and scripts/load_env.py"
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS missing — check workspace-root .env and scripts/load_env.py"
#endif

static AsyncWebServer s_server(80);
static AsyncWebSocket s_ws("/ws");

static void on_ws_event(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server;
  (void)arg;
  (void)data;
  (void)len;
  if (type == WS_EVT_CONNECT) {
    client->text("ready");
  }
}

static void send_asset(AsyncWebServerRequest *request, const char *body,
                       const char *content_type) {
  AsyncWebServerResponse *response =
      request->beginResponse(200, content_type, body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

bool web_begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("diwali-cracker");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("Wi-Fi connect timed out");
      return false;
    }
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("Wi-Fi OK  IP=");
  Serial.println(WiFi.localIP());

  s_ws.onEvent(on_ws_event);
  s_server.addHandler(&s_ws);

  s_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_asset(request, ASSET_INDEX_HTML, "text/html; charset=utf-8");
  });
  s_server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_asset(request, ASSET_INDEX_HTML, "text/html; charset=utf-8");
  });
  s_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_asset(request, ASSET_STYLE_CSS, "text/css; charset=utf-8");
  });
  s_server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    send_asset(request, ASSET_APP_JS, "application/javascript; charset=utf-8");
  });
  s_server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
  s_server.begin();

  if (MDNS.begin("diwali-cracker")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://diwali-cracker.local/");
  }

  return true;
}

void web_loop() {
  s_ws.cleanupClients();
}

void web_broadcast(const char *message) {
  if (message == nullptr || message[0] == '\0') {
    return;
  }
  s_ws.textAll(message);
}

IPAddress web_ip() {
  return WiFi.localIP();
}
