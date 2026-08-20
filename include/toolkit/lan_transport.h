#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include <vector>

#include "toolkit/ble_provisioning.h"

namespace epd {

class LanTransport final : public ProtocolTransport {
 public:
  explicit LanTransport(BleProtocolService& protocol);

  bool begin();
  void loop();
  void stop();
  bool setSsid(const String& ssid, String& error);
  bool setPassword(const String& password, String& error);
  bool apply(String& error);
  bool forget(String& error);
  bool factoryReset(String& error);

  bool configured() const { return !ssid_.isEmpty(); }
  bool wifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  const String& ssid() const { return ssid_; }
  String ipAddress() const;
  int32_t rssi() const;
  String deviceId() const;
  String deviceKeyHex() const;

  const char* name() const override { return "lan"; }
  bool connected() const override;
  bool authenticated() const override;
  bool owner() const override { return authenticated(); }
  size_t frameBytes() const override { return kFrameBytes; }
  bool sendFrame(const uint8_t* data, size_t length) override;
  void disconnect() override;

 private:
  enum class ClientState : uint8_t { kNone, kWaitAuth, kReady };

  static constexpr uint16_t kPort = 38474;
  static constexpr size_t kFrameBytes = 1024;
  static constexpr size_t kMaxWireFrameBytes = 2048;
  static constexpr size_t kDeviceKeyBytes = 32;
  static constexpr size_t kNonceBytes = 16;
  static constexpr uint32_t kReconnectIntervalMs = 10000;
  static constexpr uint32_t kClientIdleTimeoutMs = 30000;

  bool load();
  bool saveCredentials(String& error) const;
  bool ensureDeviceKey();
  void startWifi();
  void onNetworkReady();
  void onNetworkLost();
  void acceptClient();
  void serviceClient();
  void serviceHandshake();
  void serviceFrames();
  bool authenticateLine(const String& line);
  void resetFrameReader();
  void writeGreeting();
  String hostname() const;
  static String encodeHex(const uint8_t* data, size_t length);
  static bool constantTimeEquals(const String& left, const String& right);

  BleProtocolService& protocol_;
  WiFiServer server_{kPort};
  WiFiClient client_;
  String ssid_;
  String password_;
  uint8_t device_key_[kDeviceKeyBytes]{};
  uint8_t nonce_[kNonceBytes]{};
  String handshake_line_;
  uint8_t length_bytes_[2]{};
  size_t length_size_ = 0;
  size_t expected_frame_length_ = 0;
  std::vector<uint8_t> frame_;
  ClientState client_state_ = ClientState::kNone;
  uint32_t reconnect_at_ = 0;
  uint32_t client_last_activity_ = 0;
  bool network_ready_ = false;
  bool mdns_started_ = false;
  bool active_ = false;
};

}  // namespace epd
