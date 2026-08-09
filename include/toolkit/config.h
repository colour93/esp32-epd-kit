#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace epd {

constexpr uint16_t kConfigSchemaVersion = 1;

struct DeviceSettings {
  String name = "epd-kit";
  String locale = "zh-CN";
  String timezone_iana = "Asia/Shanghai";
  String timezone_posix = "CST-8";
};

enum class WifiIpv4Mode : uint8_t { kDhcp, kStatic };

struct WifiIpv4Settings {
  WifiIpv4Mode mode = WifiIpv4Mode::kDhcp;
  String address;
  String gateway;
  String subnet;
  String dns1;
  String dns2;
};

struct WifiSettings {
  String ssid;
  String password;
  WifiIpv4Settings ipv4;
};

struct PowerSettings {
  uint32_t poll_interval_sec = 300;
  uint32_t ble_window_sec = 180;
  uint32_t offline_backoff_sec[4] = {300, 900, 1800, 3600};
};

struct DisplaySettings {
  uint16_t full_after_partial_count = 12;
  uint32_t full_max_age_sec = 86400;
  uint8_t full_area_threshold_percent = 40;
};

struct BatterySettings {
  uint16_t low_mv = 3550;
  uint16_t critical_mv = 3400;
  uint16_t recovery_mv = 3650;
};

struct HttpProxySettings {
  bool enabled = false;
  String host;
  uint16_t port = 8080;
  String username;
  String password;
};

struct CodexSettings {
  String account_id;
  String access_token;
  uint64_t expires_at = 0;
  HttpProxySettings proxy;
};

struct DeviceConfig {
  uint16_t version = kConfigSchemaVersion;
  DeviceSettings device;
  WifiSettings wifi;
  PowerSettings power;
  DisplaySettings display;
  BatterySettings battery;
  String active_app = "codex_usage";
  CodexSettings codex;

  bool isConfigured() const {
    return !wifi.ssid.isEmpty() && !codex.account_id.isEmpty() &&
           !codex.access_token.isEmpty() && active_app == "codex_usage";
  }
};

bool validateConfig(const DeviceConfig& config, String& error);
void configToJson(const DeviceConfig& config, JsonObject out, bool redact_secrets);
bool configFromJson(JsonVariantConst source, DeviceConfig& config, String& error);
bool applyConfigPatch(JsonVariantConst patch, DeviceConfig& config, String& error);

}  // namespace epd
