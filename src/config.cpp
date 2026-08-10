#include "toolkit/config.h"

namespace epd {
namespace {

bool readString(JsonVariantConst value, String& out, const char* path,
                String& error) {
  if (value.isNull()) return true;
  if (!value.is<const char*>()) {
    error = String(path) + " must be a string";
    return false;
  }
  out = value.as<const char*>();
  return true;
}

template <typename T>
bool readUnsigned(JsonVariantConst value, T& out, const char* path,
                  String& error) {
  if (value.isNull()) return true;
  if (!value.is<T>()) {
    error = String(path) + " must be an unsigned integer";
    return false;
  }
  out = value.as<T>();
  return true;
}

bool readBool(JsonVariantConst value, bool& out, const char* path,
              String& error) {
  if (value.isNull()) return true;
  if (!value.is<bool>()) {
    error = String(path) + " must be a boolean";
    return false;
  }
  out = value.as<bool>();
  return true;
}

bool requireObject(JsonVariantConst value, const char* path, String& error) {
  if (value.isNull() || value.is<JsonObjectConst>()) return true;
  error = String(path) + " must be an object";
  return false;
}

bool hasControl(const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t byte = static_cast<uint8_t>(value[i]);
    if (byte < 0x20U || byte == 0x7FU) return true;
  }
  return false;
}

bool boundedIdentifier(const String& value, size_t maximum) {
  return !value.isEmpty() && value.length() <= maximum && !hasControl(value);
}

}  // namespace

bool validateConfig(const DeviceConfig& config, String& error) {
  if (config.version != kConfigSchemaVersion) {
    error = "unsupported config version";
    return false;
  }
  if (!boundedIdentifier(config.device.name, 24) ||
      !boundedIdentifier(config.device.locale, 16) ||
      !boundedIdentifier(config.device.timezone_iana, 64)) {
    error = "invalid device name, locale, or timezone";
    return false;
  }
  const BatterySettings& battery = config.hardware.battery;
  if (!(battery.critical_mv >= 3000 &&
        battery.critical_mv < battery.low_mv &&
        battery.low_mv < battery.recovery_mv && battery.recovery_mv <= 4300)) {
    error = "battery thresholds must be ordered between 3000 and 4300 mV";
    return false;
  }
  if (config.power.wake_interval_sec < 60 ||
      config.power.wake_interval_sec > 86400) {
    error = "power wake interval must be between 60 and 86400 seconds";
    return false;
  }
  if (config.display.full_after_partial_count < 1 ||
      config.display.full_after_partial_count > 100 ||
      config.display.full_max_age_sec < 3600 ||
      config.display.full_area_threshold_percent < 10 ||
      config.display.full_area_threshold_percent > 100) {
    error = "invalid display refresh policy";
    return false;
  }
  if (!boundedIdentifier(config.view.renderer_id, 64) ||
      !boundedIdentifier(config.view.resource_key, 64)) {
    error = "invalid renderer id or resource key";
    return false;
  }
  return true;
}

void configToJson(const DeviceConfig& config, JsonObject out) {
  out["version"] = config.version;
  out["revision"] = config.revision;
  JsonObject device = out["device"].to<JsonObject>();
  device["name"] = config.device.name;
  device["locale"] = config.device.locale;
  device["timezone_iana"] = config.device.timezone_iana;

  JsonObject hardware = out["hardware"].to<JsonObject>();
  JsonObject battery = hardware["battery"].to<JsonObject>();
  battery["enabled"] = config.hardware.battery.enabled;
  battery["low_mv"] = config.hardware.battery.low_mv;
  battery["critical_mv"] = config.hardware.battery.critical_mv;
  battery["recovery_mv"] = config.hardware.battery.recovery_mv;
  JsonObject io12 = hardware["io12"].to<JsonObject>();
  io12["mode"] = config.hardware.io12_mode == Io12Mode::kKey ? "key" : "disabled";

  JsonObject power = out["power"].to<JsonObject>();
  power["profile"] = config.power.profile == PowerProfile::kBattery
                           ? "battery"
                           : "mains";
  power["wake_interval_sec"] = config.power.wake_interval_sec;

  JsonObject display = out["display"].to<JsonObject>();
  display["full_after_partial_count"] = config.display.full_after_partial_count;
  display["full_max_age_sec"] = config.display.full_max_age_sec;
  display["full_area_threshold_percent"] =
      config.display.full_area_threshold_percent;

  JsonObject view = out["view"].to<JsonObject>();
  view["renderer_id"] = config.view.renderer_id;
  view["resource_key"] = config.view.resource_key;
}

bool applyConfigPatch(JsonVariantConst source, DeviceConfig& config,
                      String& error) {
  if (!source.is<JsonObjectConst>()) {
    error = "patch must be an object";
    return false;
  }
  if (!readUnsigned(source["version"], config.version, "version", error)) {
    return false;
  }
  JsonVariantConst device = source["device"];
  if (!requireObject(device, "device", error)) return false;
  if (device.is<JsonObjectConst>() &&
      (!readString(device["name"], config.device.name, "device.name", error) ||
       !readString(device["locale"], config.device.locale, "device.locale", error) ||
       !readString(device["timezone_iana"], config.device.timezone_iana,
                   "device.timezone_iana", error))) {
    return false;
  }

  JsonVariantConst hardware = source["hardware"];
  if (!requireObject(hardware, "hardware", error)) return false;
  if (hardware.is<JsonObjectConst>()) {
    JsonVariantConst battery = hardware["battery"];
    if (!requireObject(battery, "hardware.battery", error)) return false;
    if (battery.is<JsonObjectConst>() &&
        (!readBool(battery["enabled"], config.hardware.battery.enabled,
                   "hardware.battery.enabled", error) ||
         !readUnsigned(battery["low_mv"], config.hardware.battery.low_mv,
                       "hardware.battery.low_mv", error) ||
         !readUnsigned(battery["critical_mv"],
                       config.hardware.battery.critical_mv,
                       "hardware.battery.critical_mv", error) ||
         !readUnsigned(battery["recovery_mv"],
                       config.hardware.battery.recovery_mv,
                       "hardware.battery.recovery_mv", error))) {
      return false;
    }
    JsonVariantConst io12 = hardware["io12"];
    if (!requireObject(io12, "hardware.io12", error)) return false;
    if (io12.is<JsonObjectConst>() && !io12["mode"].isNull()) {
      if (!io12["mode"].is<const char*>()) {
        error = "hardware.io12.mode must be a string";
        return false;
      }
      const String mode = io12["mode"].as<const char*>();
      if (mode == "disabled") {
        config.hardware.io12_mode = Io12Mode::kDisabled;
      } else if (mode == "key") {
        config.hardware.io12_mode = Io12Mode::kKey;
      } else {
        error = "hardware.io12.mode must be disabled or key";
        return false;
      }
    }
  }

  JsonVariantConst power = source["power"];
  if (!requireObject(power, "power", error)) return false;
  if (power.is<JsonObjectConst>()) {
    if (!power["profile"].isNull()) {
      if (!power["profile"].is<const char*>()) {
        error = "power.profile must be a string";
        return false;
      }
      const String profile = power["profile"].as<const char*>();
      if (profile == "mains") {
        config.power.profile = PowerProfile::kMains;
      } else if (profile == "battery") {
        config.power.profile = PowerProfile::kBattery;
      } else {
        error = "power.profile must be mains or battery";
        return false;
      }
    }
    if (!readUnsigned(power["wake_interval_sec"],
                      config.power.wake_interval_sec,
                      "power.wake_interval_sec", error)) {
      return false;
    }
  }

  JsonVariantConst display = source["display"];
  if (!requireObject(display, "display", error)) return false;
  if (display.is<JsonObjectConst>() &&
      (!readUnsigned(display["full_after_partial_count"],
                     config.display.full_after_partial_count,
                     "display.full_after_partial_count", error) ||
       !readUnsigned(display["full_max_age_sec"],
                     config.display.full_max_age_sec,
                     "display.full_max_age_sec", error) ||
       !readUnsigned(display["full_area_threshold_percent"],
                     config.display.full_area_threshold_percent,
                     "display.full_area_threshold_percent", error))) {
    return false;
  }

  JsonVariantConst view = source["view"];
  if (!requireObject(view, "view", error)) return false;
  if (view.is<JsonObjectConst>() &&
      (!readString(view["renderer_id"], config.view.renderer_id,
                   "view.renderer_id", error) ||
       !readString(view["resource_key"], config.view.resource_key,
                   "view.resource_key", error))) {
    return false;
  }
  return validateConfig(config, error);
}

bool configFromJson(JsonVariantConst source, DeviceConfig& config,
                    String& error) {
  if (!source.is<JsonObjectConst>()) {
    error = "config must be an object";
    return false;
  }
  DeviceConfig parsed;
  if (!source["revision"].isNull() && !source["revision"].is<uint32_t>()) {
    error = "revision must be an unsigned integer";
    return false;
  }
  parsed.revision = source["revision"] | 0U;
  if (!applyConfigPatch(source, parsed, error)) return false;
  config = parsed;
  return true;
}

bool configRequiresRestart(const DeviceConfig& before,
                           const DeviceConfig& after) {
  return before.hardware.battery.enabled != after.hardware.battery.enabled ||
         before.hardware.io12_mode != after.hardware.io12_mode ||
         before.power.profile != after.power.profile;
}

}  // namespace epd
