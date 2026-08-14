#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace epd {

constexpr uint16_t kConfigSchemaVersion = 4;
constexpr size_t kMaxPageBindings = 8;
constexpr size_t kSlotIdMaxBytes = 32;
constexpr size_t kWidgetIdMaxBytes = 64;
constexpr size_t kPageIdMaxBytes = 64;
constexpr size_t kResourceKeyMaxBytes = 64;

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
  uint16_t full_after_partial_count = 60;
  uint32_t full_max_age_sec = 86400;
  uint8_t full_area_threshold_percent = 70;
};

struct PageBinding {
  String slot_id;
  String widget_id;
  String resource_key;
};

struct PageSettings {
  String id = "home";
  PageBinding bindings[kMaxPageBindings]{
      {"primary", "codex.usage.compact", "codex/default"}};
  size_t binding_count = 1;

  const PageBinding* findBinding(const String& slot_id) const {
    for (size_t index = 0; index < binding_count; ++index) {
      if (bindings[index].slot_id == slot_id) return &bindings[index];
    }
    return nullptr;
  }
};

struct DeviceConfig {
  uint16_t version = kConfigSchemaVersion;
  uint32_t revision = 0;
  DeviceSettings device;
  HardwareSettings hardware;
  PowerSettings power;
  DisplaySettings display;
  PageSettings page;
};

bool validateConfig(const DeviceConfig& config, String& error);
void configToJson(const DeviceConfig& config, JsonObject out);
bool configFromJson(JsonVariantConst source, DeviceConfig& config, String& error);
bool applyConfigPatch(JsonVariantConst patch, DeviceConfig& config, String& error);
bool configRequiresRestart(const DeviceConfig& before, const DeviceConfig& after);

}  // namespace epd
