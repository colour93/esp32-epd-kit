#include "toolkit/network_manager.h"

#include <WiFi.h>
#include <time.h>

namespace epd {

bool NetworkManager::connect(const WifiSettings& settings, uint32_t timeout_ms) {
  last_error_ = "";
  if (settings.ssid.isEmpty()) {
    last_error_ = "missing Wi-Fi SSID";
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  if (settings.ipv4.mode == WifiIpv4Mode::kStatic) {
    IPAddress address;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1;
    IPAddress dns2;
    if (!address.fromString(settings.ipv4.address) ||
        !gateway.fromString(settings.ipv4.gateway) ||
        !subnet.fromString(settings.ipv4.subnet) ||
        !dns1.fromString(settings.ipv4.dns1) ||
        (!settings.ipv4.dns2.isEmpty() &&
         !dns2.fromString(settings.ipv4.dns2))) {
      last_error_ = "invalid static IPv4 configuration";
      disconnect();
      return false;
    }
    if (!WiFi.config(address, gateway, subnet, dns1, dns2)) {
      last_error_ = "cannot apply static IPv4 configuration";
      disconnect();
      return false;
    }
  } else if (!WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress(),
                          IPAddress())) {
    // A zero local address restarts the ESP-IDF DHCP client and clears any
    // static settings left by a preceding wifi.test in the same boot.
    last_error_ = "cannot enable DHCP";
    disconnect();
    return false;
  }
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

  const uint32_t started_at = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started_at < timeout_ms) {
    delay(50);
  }
  if (WiFi.status() != WL_CONNECTED) {
    last_error_ = "Wi-Fi connection timed out";
    disconnect();
    return false;
  }
  return true;
}

bool NetworkManager::syncClock(const DeviceSettings& settings, uint32_t timeout_ms) {
  if (!connected()) {
    last_error_ = "cannot sync time while offline";
    return false;
  }

  setenv("TZ", settings.timezone_posix.c_str(), 1);
  tzset();
  if (clockValid()) return true;

  configTzTime(settings.timezone_posix.c_str(), "time.cloudflare.com", "pool.ntp.org",
               "time.google.com");
  const uint32_t started_at = millis();
  while (!clockValid() && millis() - started_at < timeout_ms) delay(50);
  if (!clockValid()) {
    last_error_ = "SNTP synchronization timed out";
    return false;
  }
  return true;
}

void NetworkManager::disconnect() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

bool NetworkManager::connected() const { return WiFi.status() == WL_CONNECTED; }

bool NetworkManager::clockValid() {
  return static_cast<uint64_t>(time(nullptr)) >= 1700000000ULL;
}

}  // namespace epd
