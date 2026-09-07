#pragma once

#include "V5SyncMedia.h"
#include <WiFi.h>
#include <base64.h>
#include <mbedtls/sha1.h>

// One-client, control-only WebSocket. Frames are deliberately bounded to 125
// bytes and never carry video data.
class V5SyncSocket {
public:
  void begin(V5SyncMedia &media) {
    sync = &media;
    stop();
    listener.begin();
    listening = true;
  }
  void stop() {
    if (client)
      client.stop();
    if (listening)
      listener.stop();
    listening = false;
    upgraded = false;
    handshake = "";
    received = 0;
  }
  void service() {
    if (!listening || !sync)
      return;
    if (!client || !client.connected()) {
      if (client)
        client.stop();
      client = listener.accept();
      if (!client)
        return;
      if (client.localIP() != WiFi.softAPIP()) {
        client.stop();
        return;
      }
      client.setNoDelay(true);
      connectedAt = millis();
      upgraded = false;
      handshake = "";
      received = 0;
    }
    if (!upgraded)
      return serviceHandshake();
    readFrames();
  }
  bool takeActivity() {
    const bool value = activity;
    activity = false;
    return value;
  }

private:
  WiFiServer listener{81};
  WiFiClient client;
  V5SyncMedia *sync = nullptr;
  bool listening = false, upgraded = false, activity = false;
  uint32_t connectedAt = 0;
  String handshake;
  uint8_t input[256]{};
  size_t received = 0;

  static bool header(const String &request, const char *name, String &value) {
    int start = request.indexOf("\r\n") + 2;
    while (start >= 2 && start < int(request.length())) {
      const int end = request.indexOf("\r\n", start);
      if (end < 0 || end == start)
        break;
      const int colon = request.indexOf(':', start);
      if (colon > start && colon < end) {
        String key = request.substring(start, colon);
        key.trim();
        if (key.equalsIgnoreCase(name)) {
          value = request.substring(colon + 1, end);
          value.trim();
          return true;
        }
      }
      start = end + 2;
    }
    return false;
  }
  void serviceHandshake() {
    size_t budget = 512;
    while (budget-- && client.available()) {
      const int value = client.read();
      if (value < 0)
        break;
      if (handshake.length() >= 2048) {
        client.stop();
        return;
      }
      handshake += char(value);
      if (!handshake.endsWith("\r\n\r\n"))
        continue;
      String key, upgrade, connection, version;
      const int first = handshake.indexOf("\r\n");
      if (first <= 0 ||
          handshake.substring(0, first) != "GET /sync-ws HTTP/1.1" ||
          !header(handshake, "Sec-WebSocket-Key", key) ||
          !header(handshake, "Upgrade", upgrade) ||
          !header(handshake, "Connection", connection) ||
          !header(handshake, "Sec-WebSocket-Version", version)) {
        client.stop();
        return;
      }
      upgrade.toLowerCase();
      connection.toLowerCase();
      if (upgrade != "websocket" || connection.indexOf("upgrade") < 0 ||
          version != "13" || key.length() < 16 || key.length() > 64) {
        client.stop();
        return;
      }
      String source = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
      uint8_t digest[20]{};
      if (mbedtls_sha1(reinterpret_cast<const unsigned char *>(source.c_str()),
                       source.length(), digest) != 0) {
        client.stop();
        return;
      }
      const String accept = base64::encode(digest, sizeof(digest));
      const String response =
          "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
          "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept +
          "\r\n\r\n";
      if (client.write(reinterpret_cast<const uint8_t *>(response.c_str()),
                       response.length()) != response.length()) {
        client.stop();
        return;
      }
      upgraded = true;
      handshake = "";
      activity = true;
      return;
    }
    if (millis() - connectedAt > 5000)
      client.stop();
  }
  bool sendFrame(uint8_t opcode, const uint8_t *data, size_t size) {
    if (!client || !upgraded || size > 125)
      return false;
    const uint8_t head[2] = {uint8_t(0x80U | opcode), uint8_t(size)};
    return writeAll(head, sizeof(head)) && (!size || writeAll(data, size));
  }
  void sendStatus(uint8_t controlSequence = 0, bool accepted = true) {
    uint8_t data[16] = {'M', 'S', 'A', '1'};
    put32(data + 4, millis());
    put32(data + 8, sync->positionMs(millis()));
    data[12] = uint8_t(sync->state);
    data[13] = sync->browserEventSequence;
    data[14] = controlSequence;
    data[15] = accepted ? 1 : 0;
    if (!sendFrame(2, data, sizeof(data)))
      client.stop();
  }
  void readFrames() {
    while (client.available() && received < sizeof(input)) {
      const int count = client.read(input + received, sizeof(input) - received);
      if (count <= 0)
        break;
      received += size_t(count);
    }
    while (received >= 2) {
      const uint8_t opcode = input[0] & 15U;
      const bool fin = input[0] & 0x80U, masked = input[1] & 0x80U;
      const size_t length = input[1] & 0x7FU;
      if (!fin || (input[0] & 0x70U) || !masked || length == 126 ||
          length == 127 ||
          6U + length > sizeof(input)) {
        client.stop();
        return;
      }
      const size_t total = 6U + length;
      if (received < total)
        return;
      uint8_t *payload = input + 6;
      for (size_t i = 0; i < length; ++i)
        payload[i] ^= input[2 + (i & 3U)];
      if (opcode == 2 && (length == 9 || length == 10) &&
          !memcmp(payload, "MSC1", 4)) {
        const uint32_t position = read32(payload + 4);
        const uint8_t sequence = length == 10 ? payload[9] : 0;
        const bool accepted =
            sync->control(position, payload[8] != 0, millis());
        activity = true;
        sendStatus(sequence, accepted);
      } else if (opcode == 9) {
        sendFrame(10, payload, length);
      } else if (opcode == 8) {
        sendFrame(8, payload, length);
        client.stop();
        return;
      } else {
        client.stop();
        return;
      }
      memmove(input, input + total, received - total);
      received -= total;
    }
    if (received == sizeof(input))
      client.stop();
  }
  static uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
  }
  bool writeAll(const uint8_t *data, size_t size) {
    const uint32_t started = millis();
    size_t sent = 0;
    while (sent < size && client.connected() && millis() - started < 250) {
      const size_t count = client.write(data + sent, size - sent);
      if (count)
        sent += count;
      else
        delay(0);
    }
    return sent == size;
  }
  static void put32(uint8_t *p, uint32_t value) {
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
  }
};
