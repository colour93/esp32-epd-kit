#include "toolkit/config.h"

#include <algorithm>

#include "toolkit/core_logic.h"

namespace epd {
namespace {

template <typename T>
bool readNumber(JsonVariantConst value, T& destination, const char* path,
                String& error) {
  if (value.isNull()) return true;
  if (!value.is<T>()) {
    error = String(path) + " must be an unsigned integer";
    return false;
  }
  destination = value.as<T>();
  return true;
}

bool readString(JsonVariantConst value, String& destination, const char* path,
                String& error) {
  if (value.isNull()) return true;
  if (!value.is<const char*>()) {
    error = String(path) + " must be a string";
    return false;
  }
  destination = value.as<const char*>();
  return true;
}

bool requireObjectOrNull(JsonVariantConst value, const char* path,
                         String& error) {
  if (value.isNull() || value.is<JsonObjectConst>()) return true;
  error = String(path) + " must be an object";
  return false;
}

bool hasControlCharacters(const String& value) {
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t byte = static_cast<uint8_t>(value[index]);
    if (byte < 0x20U || byte == 0x7FU) return true;
  }
  return false;
}

std::string_view asView(const String& value) {
  return {value.c_str(), value.length()};
}

}  // namespace

bool validateConfig(const DeviceConfig& config, String& error) {
  if (config.version != kConfigSchemaVersion) {
    error = "unsupported config version";
    return false;
  }
  if (config.device.name.isEmpty() || config.device.name.length() > 24 ||
      hasControlCharacters(config.device.name)) {
    error = "device.name must contain 1-24 printable bytes";
    return false;
  }
  if (config.device.locale.length() > 16 || config.device.timezone_iana.length() > 64 ||
      config.device.timezone_posix.isEmpty() || config.device.timezone_posix.length() > 96) {
    error = "invalid locale or timezone";
    return false;
  }
  if (config.wifi.ssid.length() > 32 || config.wifi.password.length() > 64) {
    error = "Wi-Fi credentials exceed ESP32 limits";
    return false;
  }
  if (config.wifi.ipv4.mode == WifiIpv4Mode::kStatic) {
    if (!core::isValidStaticIpv4(
            asView(config.wifi.ipv4.address),
            asView(config.wifi.ipv4.gateway),
            asView(config.wifi.ipv4.subnet), asView(config.wifi.ipv4.dns1),
            asView(config.wifi.ipv4.dns2))) {
      error = "invalid static IPv4 host, gateway, subnet, or DNS";
      return false;
    }
  }
  if (config.power.poll_interval_sec < 60 || config.power.poll_interval_sec > 86400 ||
      config.power.ble_window_sec < 30 || config.power.ble_window_sec > 600) {
    error = "power interval outside supported range";
    return false;
  }
  for (uint32_t delay : config.power.offline_backoff_sec) {
    if (delay < 60 || delay > 86400) {
      error = "offline backoff outside supported range";
      return false;
    }
  }
  if (config.display.full_after_partial_count < 1 ||
      config.display.full_after_partial_count > 100 ||
      config.display.full_area_threshold_percent < 10 ||
      config.display.full_area_threshold_percent > 100 ||
      config.display.full_max_age_sec < 3600) {
    error = "invalid display refresh policy";
    return false;
  }
  if (!(config.battery.critical_mv < config.battery.low_mv &&
        config.battery.low_mv < config.battery.recovery_mv &&
        config.battery.critical_mv >= 3000 && config.battery.recovery_mv <= 4300)) {
    error = "battery thresholds must be ordered and safe";
    return false;
  }
  if (config.active_app != "codex_usage") {
    error = "unknown active_app";
    return false;
  }
  if (config.codex.account_id.length() > 128 || config.codex.access_token.length() > 4096) {
    error = "Codex credential exceeds protocol limits";
    return false;
  }
  if ((!config.codex.proxy.host.isEmpty() &&
       !core::isValidHttpProxyHost(asView(config.codex.proxy.host))) ||
      config.codex.proxy.username.length() > 128 ||
      config.codex.proxy.password.length() > 256 ||
      config.codex.proxy.username.indexOf(':') >= 0 ||
      hasControlCharacters(config.codex.proxy.username) ||
      hasControlCharacters(config.codex.proxy.password)) {
    error = "invalid HTTP proxy credentials";
    return false;
  }
  if (config.codex.proxy.port == 0) {
    error = "HTTP proxy port must be between 1 and 65535";
    return false;
  }
  if (config.codex.proxy.enabled &&
      (config.codex.proxy.host.isEmpty() ||
       (config.codex.proxy.username.isEmpty() &&
        !config.codex.proxy.password.isEmpty()))) {
    error = "enabled HTTP proxy requires host, port, and valid credentials";
    return false;
  }
  return true;
}

void configToJson(const DeviceConfig& config, JsonObject out, bool redact_secrets) {
  out["version"] = config.version;
  JsonObject device = out["device"].to<JsonObject>();
  device["name"] = config.device.name;
  device["locale"] = config.device.locale;
  JsonObject timezone = device["timezone"].to<JsonObject>();
  timezone["iana"] = config.device.timezone_iana;
  timezone["posix"] = config.device.timezone_posix;

  JsonObject wifi = out["wifi"].to<JsonObject>();
  wifi["ssid"] = config.wifi.ssid;
  if (redact_secrets) {
    wifi["password_set"] = !config.wifi.password.isEmpty();
  } else {
    wifi["password"] = config.wifi.password;
  }
  JsonObject ipv4 = wifi["ipv4"].to<JsonObject>();
  ipv4["mode"] = config.wifi.ipv4.mode == WifiIpv4Mode::kStatic ? "static"
                                                                      : "dhcp";
  ipv4["address"] = config.wifi.ipv4.address;
  ipv4["gateway"] = config.wifi.ipv4.gateway;
  ipv4["subnet"] = config.wifi.ipv4.subnet;
  ipv4["dns1"] = config.wifi.ipv4.dns1;
  ipv4["dns2"] = config.wifi.ipv4.dns2;

  JsonObject power = out["power"].to<JsonObject>();
  power["poll_interval_sec"] = config.power.poll_interval_sec;
  power["ble_window_sec"] = config.power.ble_window_sec;
  JsonArray backoff = power["offline_backoff_sec"].to<JsonArray>();
  for (uint32_t delay : config.power.offline_backoff_sec) backoff.add(delay);

  JsonObject display = out["display"].to<JsonObject>();
  display["full_after_partial_count"] = config.display.full_after_partial_count;
  display["full_max_age_sec"] = config.display.full_max_age_sec;
  display["full_area_threshold_percent"] =
      config.display.full_area_threshold_percent;

  JsonObject battery = out["battery"].to<JsonObject>();
  battery["low_mv"] = config.battery.low_mv;
  battery["critical_mv"] = config.battery.critical_mv;
  battery["recovery_mv"] = config.battery.recovery_mv;

  out["active_app"] = config.active_app;
  JsonObject apps = out["apps"].to<JsonObject>();
  JsonObject codex = apps["codex_usage"].to<JsonObject>();
  codex["account_id"] = config.codex.account_id;
  codex["expires_at"] = config.codex.expires_at;
  if (redact_secrets) {
    codex["access_token_set"] = !config.codex.access_token.isEmpty();
  } else {
    codex["access_token"] = config.codex.access_token;
  }
  JsonObject proxy = codex["proxy"].to<JsonObject>();
  proxy["enabled"] = config.codex.proxy.enabled;
  proxy["host"] = config.codex.proxy.host;
  proxy["port"] = config.codex.proxy.port;
  proxy["username"] = config.codex.proxy.username;
  if (redact_secrets) {
    proxy["password_set"] = !config.codex.proxy.password.isEmpty();
  } else {
    proxy["password"] = config.codex.proxy.password;
  }
}

bool applyConfigPatch(JsonVariantConst source, DeviceConfig& config, String& error) {
  if (!source.is<JsonObjectConst>()) {
    error = "patch must be an object";
    return false;
  }

  if (!readNumber(source["version"], config.version, "version", error)) {
    return false;
  }
  JsonVariantConst device = source["device"];
  if (!requireObjectOrNull(device, "device", error)) return false;
  if (device.is<JsonObjectConst>()) {
    if (!readString(device["name"], config.device.name, "device.name", error) ||
        !readString(device["locale"], config.device.locale, "device.locale",
                    error)) {
      return false;
    }
    JsonVariantConst timezone = device["timezone"];
    if (!requireObjectOrNull(timezone, "device.timezone", error)) return false;
    if (timezone.is<JsonObjectConst>()) {
      if (!readString(timezone["iana"], config.device.timezone_iana,
                      "device.timezone.iana", error) ||
          !readString(timezone["posix"], config.device.timezone_posix,
                      "device.timezone.posix", error)) {
        return false;
      }
    }
  }

  JsonVariantConst wifi = source["wifi"];
  if (!requireObjectOrNull(wifi, "wifi", error)) return false;
  if (wifi.is<JsonObjectConst>()) {
    if (!readString(wifi["ssid"], config.wifi.ssid, "wifi.ssid", error) ||
        !readString(wifi["password"], config.wifi.password, "wifi.password",
                    error)) {
      return false;
    }
    JsonVariantConst ipv4 = wifi["ipv4"];
    if (!requireObjectOrNull(ipv4, "wifi.ipv4", error)) return false;
    if (ipv4.is<JsonObjectConst>()) {
      JsonVariantConst mode = ipv4["mode"];
      if (!mode.isNull()) {
        if (!mode.is<const char*>()) {
          error = "wifi.ipv4.mode must be a string";
          return false;
        }
        const String mode_name = mode.as<const char*>();
        if (mode_name == "dhcp") {
          config.wifi.ipv4.mode = WifiIpv4Mode::kDhcp;
        } else if (mode_name == "static") {
          config.wifi.ipv4.mode = WifiIpv4Mode::kStatic;
        } else {
          error = "wifi.ipv4.mode must be dhcp or static";
          return false;
        }
      }
      if (!readString(ipv4["address"], config.wifi.ipv4.address,
                      "wifi.ipv4.address", error) ||
          !readString(ipv4["gateway"], config.wifi.ipv4.gateway,
                      "wifi.ipv4.gateway", error) ||
          !readString(ipv4["subnet"], config.wifi.ipv4.subnet,
                      "wifi.ipv4.subnet", error) ||
          !readString(ipv4["dns1"], config.wifi.ipv4.dns1,
                      "wifi.ipv4.dns1", error) ||
          !readString(ipv4["dns2"], config.wifi.ipv4.dns2,
                      "wifi.ipv4.dns2", error)) {
        return false;
      }
    }
  }

  JsonVariantConst power = source["power"];
  if (!requireObjectOrNull(power, "power", error)) return false;
  if (power.is<JsonObjectConst>()) {
    if (!readNumber(power["poll_interval_sec"],
                    config.power.poll_interval_sec,
                    "power.poll_interval_sec", error) ||
        !readNumber(power["ble_window_sec"], config.power.ble_window_sec,
                    "power.ble_window_sec", error)) {
      return false;
    }
    JsonVariantConst backoff_value = power["offline_backoff_sec"];
    if (!backoff_value.isNull()) {
      if (!backoff_value.is<JsonArrayConst>()) {
        error = "power.offline_backoff_sec must be an array";
        return false;
      }
      JsonArrayConst backoff = backoff_value.as<JsonArrayConst>();
      if (backoff.size() != 4) {
        error = "offline_backoff_sec must contain four values";
        return false;
      }
      for (size_t i = 0; i < 4; ++i) {
        if (!backoff[i].is<uint32_t>()) {
          error = "power.offline_backoff_sec values must be unsigned integers";
          return false;
        }
        config.power.offline_backoff_sec[i] = backoff[i].as<uint32_t>();
      }
    }
  }

  JsonVariantConst display = source["display"];
  if (!requireObjectOrNull(display, "display", error)) return false;
  if (display.is<JsonObjectConst>()) {
    if (!readNumber(display["full_after_partial_count"],
                    config.display.full_after_partial_count,
                    "display.full_after_partial_count", error) ||
        !readNumber(display["full_max_age_sec"],
                    config.display.full_max_age_sec,
                    "display.full_max_age_sec", error) ||
        !readNumber(display["full_area_threshold_percent"],
                    config.display.full_area_threshold_percent,
                    "display.full_area_threshold_percent", error)) {
      return false;
    }
  }

  JsonVariantConst battery = source["battery"];
  if (!requireObjectOrNull(battery, "battery", error)) return false;
  if (battery.is<JsonObjectConst>()) {
    if (!readNumber(battery["low_mv"], config.battery.low_mv,
                    "battery.low_mv", error) ||
        !readNumber(battery["critical_mv"], config.battery.critical_mv,
                    "battery.critical_mv", error) ||
        !readNumber(battery["recovery_mv"], config.battery.recovery_mv,
                    "battery.recovery_mv", error)) {
      return false;
    }
  }

  if (!readString(source["active_app"], config.active_app, "active_app",
                  error)) {
    return false;
  }
  JsonVariantConst apps = source["apps"];
  if (!requireObjectOrNull(apps, "apps", error)) return false;
  if (apps.is<JsonObjectConst>()) {
    JsonVariantConst codex = apps["codex_usage"];
    if (!requireObjectOrNull(codex, "apps.codex_usage", error)) return false;
    if (codex.is<JsonObjectConst>()) {
      if (!readString(codex["account_id"], config.codex.account_id,
                      "apps.codex_usage.account_id", error) ||
          !readString(codex["access_token"], config.codex.access_token,
                      "apps.codex_usage.access_token", error) ||
          !readNumber(codex["expires_at"], config.codex.expires_at,
                      "apps.codex_usage.expires_at", error)) {
        return false;
      }
      JsonVariantConst proxy = codex["proxy"];
      if (!requireObjectOrNull(proxy, "apps.codex_usage.proxy", error)) {
        return false;
      }
      if (proxy.is<JsonObjectConst>()) {
        JsonVariantConst enabled = proxy["enabled"];
        if (!enabled.isNull()) {
          if (!enabled.is<bool>()) {
            error = "apps.codex_usage.proxy.enabled must be a boolean";
            return false;
          }
          config.codex.proxy.enabled = enabled.as<bool>();
        }
        if (!readString(proxy["host"], config.codex.proxy.host,
                        "apps.codex_usage.proxy.host", error) ||
            !readNumber(proxy["port"], config.codex.proxy.port,
                        "apps.codex_usage.proxy.port", error) ||
            !readString(proxy["username"], config.codex.proxy.username,
                        "apps.codex_usage.proxy.username", error) ||
            !readString(proxy["password"], config.codex.proxy.password,
                        "apps.codex_usage.proxy.password", error)) {
          return false;
        }
      }
    }
  }

  return validateConfig(config, error);
}

bool configFromJson(JsonVariantConst source, DeviceConfig& config, String& error) {
  DeviceConfig parsed;
  if (!applyConfigPatch(source, parsed, error)) return false;
  config = parsed;
  return true;
}

}  // namespace epd
