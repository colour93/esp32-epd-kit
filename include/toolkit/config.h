#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace epd {

constexpr uint16_t kConfigSchemaVersion = 3;

enum class PowerProfile : uint8_t { kMains, kBattery };
enum class Io12Mode : uint8_t { kDisabled, kKey };

struct DeviceSettings {
  String name = "epd-kit";
  String locale = "zh-CN";
  String timezone_iana = "Asia/Shanghai";
};

struct BatterySettings {
  bool enabled = false;
  uint16_t low_mv = 3550;
  uint16_t critical_mv = 3400;
  uint16_t recovery_mv = 3650;
};

struct HardwareSettings {
  BatterySettings battery;
  Io12Mode io12_mode = Io12Mode::kDisabled;
};

struct PowerSettings {
  PowerProfile profile = PowerProfile::kMains;
  uint32_t wake_interval_sec = 300;
};

struct DisplaySettings {
  uint16_t full_after_partial_count = 12;
  uint32_t full_max_age_sec = 86400;
  uint8_t full_area_threshold_percent = 40;
};

struct ViewSettings {
  String renderer_id = "codex.rate_limits";
  String resource_key = "codex/default";
};

struct DeviceConfig {
  uint16_t version = kConfigSchemaVersion;
  uint32_t revision = 0;
  DeviceSettings device;
  HardwareSettings hardware;
  PowerSettings power;
  DisplaySettings display;
  ViewSettings view;
};

bool validateConfig(const DeviceConfig& config, String& error);
void configToJson(const DeviceConfig& config, JsonObject out);
bool configFromJson(JsonVariantConst source, DeviceConfig& config, String& error);
bool applyConfigPatch(JsonVariantConst patch, DeviceConfig& config, String& error);
bool configRequiresRestart(const DeviceConfig& before, const DeviceConfig& after);

}  // namespace epd
