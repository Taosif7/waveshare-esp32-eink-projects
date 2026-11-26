#include "radio.h"

#include <Network.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {

constexpr uint8_t kBroadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

QueueHandle_t gRx = nullptr;
uint8_t gSeq = 1;
pond::Dedup gDedup = {};

void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  if (gRx == nullptr || data == nullptr) {
    return;
  }
  pond::Packet packet;
  if (!pond::parse_packet(data, static_cast<size_t>(len), &packet)) {
    return;
  }
  xQueueSend(gRx, &packet, 0);
}

}  // namespace

bool radio_begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  WiFi.setSleep(false);
  delay(80);
  esp_wifi_set_channel(pond::kWifiChannel, WIFI_SECOND_CHAN_NONE);

  if (gRx == nullptr) {
    gRx = xQueueCreate(8, sizeof(pond::Packet));
  }
  if (gRx == nullptr) {
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    delay(40);
    if (esp_now_init() != ESP_OK) {
      Serial.println("ESP-NOW init failed");
      return false;
    }
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = pond::kWifiChannel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(kBroadcast)) {
    if (esp_now_add_peer(&peer) != ESP_OK) {
      Serial.println("ESP-NOW broadcast peer failed");
      return false;
    }
  }
  esp_now_register_recv_cb(on_recv);
  Serial.printf("ESP-NOW channel %d, broadcast\n", pond::kWifiChannel);
  return true;
}

uint8_t radio_take_seq() { return gSeq++; }

void radio_send(const pond::Packet &packet) {
  for (int attempt = 0; attempt < pond::kRadioRetries; ++attempt) {
    esp_now_send(kBroadcast, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
    if (attempt + 1 < pond::kRadioRetries) {
      delay(pond::kRadioRetryMs);
    }
  }
}

void radio_poll(void (*on_packet)(const pond::Packet &packet)) {
  if (gRx == nullptr || on_packet == nullptr) {
    return;
  }
  pond::Packet packet;
  while (xQueueReceive(gRx, &packet, 0) == pdTRUE) {
    if (!pond::dedup_accept(&gDedup, packet)) {
      continue;
    }
    on_packet(packet);
  }
}
