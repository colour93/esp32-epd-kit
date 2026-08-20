#include "toolkit/lan_transport.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/md.h>

#include <algorithm>

#include "toolkit/log.h"

namespace epd {
namespace {

constexpr const char* kPreferencesNamespace = "epd_net4";
constexpr const char* kSsidKey = "ssid";
constexpr const char* kPasswordKey = "pass";
constexpr const char* kDeviceKey = "key";

bool deadlineReached(uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(millis() - deadline) >= 0;
}

}  // namespace

LanTransport::LanTransport(BleProtocolService& protocol) : protocol_(protocol) {}

bool LanTransport::begin() {
  if (active_) return true;
  protocol_.attachTransport(*this);
  if (!load() || !ensureDeviceKey()) {
    TOOLKIT_LOG("lan", "cannot initialize network settings");
    return false;
  }
  active_ = true;
  if (configured()) startWifi();
  TOOLKIT_LOG("lan", String("transport ready configured=") +
                         (configured() ? "yes" : "no") +
                         " id=" + deviceId());
  return true;
}

bool LanTransport::load() {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) return false;
  ssid_ = preferences.getString(kSsidKey, "");
  password_ = preferences.getString(kPasswordKey, "");
  if (preferences.getBytesLength(kDeviceKey) == kDeviceKeyBytes) {
    preferences.getBytes(kDeviceKey, device_key_, sizeof(device_key_));
  }
  preferences.end();
  return true;
}

bool LanTransport::ensureDeviceKey() {
  bool empty = true;
  for (uint8_t byte : device_key_) empty &= byte == 0;
  if (!empty) return true;
  esp_fill_random(device_key_, sizeof(device_key_));
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) return false;
  const size_t written =
      preferences.putBytes(kDeviceKey, device_key_, sizeof(device_key_));
  preferences.end();
  return written == sizeof(device_key_);
}

bool LanTransport::saveCredentials(String& error) const {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    error = "cannot open network settings";
    return false;
  }
  const bool saved = preferences.putString(kSsidKey, ssid_) == ssid_.length() &&
                     preferences.putString(kPasswordKey, password_) ==
                         password_.length();
  preferences.end();
  if (!saved) error = "cannot save WiFi credentials";
  return saved;
}

bool LanTransport::setSsid(const String& ssid, String& error) {
  if (ssid.isEmpty() || ssid.length() > 32) {
    error = "SSID must contain 1 to 32 bytes";
    return false;
  }
  ssid_ = ssid;
  return saveCredentials(error);
}

bool LanTransport::setPassword(const String& password, String& error) {
  if (!password.isEmpty() &&
      (password.length() < 8 || password.length() > 63)) {
    error = "WiFi password must be empty or contain 8 to 63 bytes";
    return false;
  }
  password_ = password;
  return saveCredentials(error);
}

bool LanTransport::apply(String& error) {
  if (!configured()) {
    error = "configure an SSID first";
    return false;
  }
  if (!saveCredentials(error)) return false;
  stop();
  active_ = true;
  protocol_.attachTransport(*this);
  startWifi();
  return true;
}

bool LanTransport::forget(String& error) {
  stop();
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    error = "cannot open network settings";
    return false;
  }
  const bool removed_ssid = preferences.remove(kSsidKey) ||
                            !preferences.isKey(kSsidKey);
  const bool removed_password = preferences.remove(kPasswordKey) ||
                                !preferences.isKey(kPasswordKey);
  preferences.end();
  if (!removed_ssid || !removed_password) {
    error = "cannot clear WiFi credentials";
    return false;
  }
  ssid_ = "";
  password_ = "";
  active_ = true;
  protocol_.attachTransport(*this);
  return true;
}

bool LanTransport::factoryReset(String& error) {
  stop();
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    error = "cannot open network settings";
    return false;
  }
  const bool cleared = preferences.clear();
  preferences.end();
  if (!cleared) {
    error = "cannot erase network settings";
    return false;
  }
  ssid_ = "";
  password_ = "";
  std::fill(std::begin(device_key_), std::end(device_key_), 0);
  return true;
}

void LanTransport::startWifi() {
  if (!active_ || !configured()) return;
  const String host = hostname();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host.c_str());
  WiFi.begin(ssid_.c_str(), password_.isEmpty() ? nullptr : password_.c_str());
  reconnect_at_ = millis() + kReconnectIntervalMs;
  TOOLKIT_LOG("lan", String("connecting WiFi ssid=") + ssid_ +
                         " hostname=" + host);
}

void LanTransport::onNetworkReady() {
  network_ready_ = true;
  reconnect_at_ = 0;
  server_.begin();
  server_.setNoDelay(true);
  const String host = hostname();
  if (MDNS.begin(host.c_str())) {
    mdns_started_ = true;
    MDNS.addService("epdkit", "tcp", kPort);
    MDNS.addServiceTxt("epdkit", "tcp", "id", deviceId());
    MDNS.addServiceTxt("epdkit", "tcp", "name", host);
    MDNS.addServiceTxt("epdkit", "tcp", "proto", "4");
    MDNS.addServiceTxt("epdkit", "tcp", "fw", EPD_TOOLKIT_VERSION);
  } else {
    TOOLKIT_LOG("lan", "mDNS initialization failed; manual IP remains available");
  }
  TOOLKIT_LOG("lan", String("WiFi connected ip=") + ipAddress() +
                         " rssi=" + rssi() + " port=" + kPort);
}

void LanTransport::onNetworkLost() {
  disconnect();
  server_.end();
  if (mdns_started_) MDNS.end();
  mdns_started_ = false;
  network_ready_ = false;
  reconnect_at_ = millis() + kReconnectIntervalMs;
  TOOLKIT_LOG("lan", "WiFi connection lost");
}

void LanTransport::loop() {
  if (!active_ || !configured()) return;
  if (WiFi.status() == WL_CONNECTED) {
    if (!network_ready_) onNetworkReady();
    acceptClient();
    serviceClient();
    return;
  }
  if (network_ready_) onNetworkLost();
  if (deadlineReached(reconnect_at_)) {
    WiFi.disconnect(false, false);
    startWifi();
  }
}

void LanTransport::acceptClient() {
  WiFiClient candidate = server_.available();
  if (!candidate) return;
  if (client_state_ != ClientState::kNone) disconnect();
  client_ = candidate;
  client_.setNoDelay(true);
  client_state_ = ClientState::kWaitAuth;
  client_last_activity_ = millis();
  handshake_line_ = "";
  resetFrameReader();
  writeGreeting();
  TOOLKIT_LOG("lan", "TCP client accepted; authentication required");
}

void LanTransport::writeGreeting() {
  esp_fill_random(nonce_, sizeof(nonce_));
  const String nonce = encodeHex(nonce_, sizeof(nonce_));
  client_.print("EPD4 ");
  client_.print(deviceId());
  client_.print(' ');
  client_.print(nonce);
  client_.print('\n');
}

void LanTransport::serviceClient() {
  if (client_state_ == ClientState::kNone) return;
  if (!client_ || !client_.connected()) {
    disconnect();
    return;
  }
  if (millis() - client_last_activity_ > kClientIdleTimeoutMs) {
    TOOLKIT_LOG("lan", "TCP client idle timeout");
    disconnect();
    return;
  }
  if (client_state_ == ClientState::kWaitAuth) {
    serviceHandshake();
  } else {
    serviceFrames();
  }
}

void LanTransport::serviceHandshake() {
  while (client_.available() > 0) {
    const char current = static_cast<char>(client_.read());
    client_last_activity_ = millis();
    if (current == '\r') continue;
    if (current == '\n') {
      const String line = handshake_line_;
      handshake_line_ = "";
      if (!authenticateLine(line)) {
        client_.print("ERR authentication\n");
        TOOLKIT_LOG("security", "LAN authentication rejected");
        disconnect();
        return;
      }
      client_state_ = ClientState::kReady;
      client_.print("OK ");
      client_.print(kFrameBytes);
      client_.print('\n');
      protocol_.transportConnected(*this);
      TOOLKIT_LOG("security", "LAN client authenticated as physical owner");
      return;
    }
    if (handshake_line_.length() >= 160) {
      disconnect();
      return;
    }
    handshake_line_ += current;
  }
}

bool LanTransport::authenticateLine(const String& line) {
  if (!line.startsWith("AUTH ") || line.length() != 69) return false;
  const String supplied = line.substring(5);
  const String nonce = encodeHex(nonce_, sizeof(nonce_));
  const String message = "EPD4:" + deviceId() + ":" + nonce;
  uint8_t digest[32]{};
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr ||
      mbedtls_md_hmac(info, device_key_, sizeof(device_key_),
                      reinterpret_cast<const unsigned char*>(message.c_str()),
                      message.length(), digest) != 0) {
    return false;
  }
  return constantTimeEquals(supplied, encodeHex(digest, sizeof(digest)));
}

void LanTransport::serviceFrames() {
  while (client_.available() > 0) {
    client_last_activity_ = millis();
    if (length_size_ < sizeof(length_bytes_)) {
      length_bytes_[length_size_++] = static_cast<uint8_t>(client_.read());
      if (length_size_ < sizeof(length_bytes_)) continue;
      expected_frame_length_ = static_cast<size_t>(length_bytes_[0]) |
                               (static_cast<size_t>(length_bytes_[1]) << 8U);
      if (expected_frame_length_ < 8 ||
          expected_frame_length_ > kMaxWireFrameBytes) {
        TOOLKIT_LOG("lan", "invalid TCP frame length");
        disconnect();
        return;
      }
      frame_.clear();
      frame_.reserve(expected_frame_length_);
    }
    const size_t remaining = expected_frame_length_ - frame_.size();
    const size_t available = static_cast<size_t>(client_.available());
    const size_t chunk = std::min(remaining, available);
    for (size_t index = 0; index < chunk; ++index) {
      frame_.push_back(static_cast<uint8_t>(client_.read()));
    }
    if (frame_.size() == expected_frame_length_) {
      protocol_.receiveTransportFrame(*this, frame_.data(), frame_.size());
      resetFrameReader();
    }
  }
}

void LanTransport::resetFrameReader() {
  length_size_ = 0;
  expected_frame_length_ = 0;
  frame_.clear();
}

bool LanTransport::connected() const {
  return client_state_ == ClientState::kReady;
}

bool LanTransport::authenticated() const { return connected(); }

bool LanTransport::sendFrame(const uint8_t* data, size_t length) {
  if (!connected() || length < 8 || length > kMaxWireFrameBytes) return false;
  const uint8_t prefix[2] = {static_cast<uint8_t>(length & 0xFFU),
                             static_cast<uint8_t>((length >> 8U) & 0xFFU)};
  if (client_.write(prefix, sizeof(prefix)) != sizeof(prefix) ||
      client_.write(data, length) != length) {
    return false;
  }
  client_last_activity_ = millis();
  return true;
}

void LanTransport::disconnect() {
  const bool was_ready = client_state_ == ClientState::kReady;
  if (client_) client_.stop();
  client_state_ = ClientState::kNone;
  handshake_line_ = "";
  resetFrameReader();
  if (was_ready) protocol_.transportDisconnected(*this);
}

void LanTransport::stop() {
  if (!active_ && !network_ready_) return;
  disconnect();
  server_.end();
  if (mdns_started_) MDNS.end();
  mdns_started_ = false;
  network_ready_ = false;
  reconnect_at_ = 0;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  active_ = false;
}

String LanTransport::ipAddress() const {
  return wifiConnected() ? WiFi.localIP().toString() : String();
}

int32_t LanTransport::rssi() const {
  return wifiConnected() ? WiFi.RSSI() : 0;
}

String LanTransport::deviceId() const {
  const uint64_t mac = ESP.getEfuseMac();
  char formatted[13];
  snprintf(formatted, sizeof(formatted), "%04lX%08lX",
           static_cast<unsigned long>((mac >> 32U) & 0xFFFFULL),
           static_cast<unsigned long>(mac & 0xFFFFFFFFULL));
  return formatted;
}

String LanTransport::deviceKeyHex() const {
  return encodeHex(device_key_, sizeof(device_key_));
}

String LanTransport::hostname() const {
  const String id = deviceId();
  return "epd-kit-" + id.substring(id.length() - 6);
}

String LanTransport::encodeHex(const uint8_t* data, size_t length) {
  static constexpr char kHex[] = "0123456789abcdef";
  String encoded;
  encoded.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    encoded += kHex[data[index] >> 4U];
    encoded += kHex[data[index] & 0x0FU];
  }
  return encoded;
}

bool LanTransport::constantTimeEquals(const String& left,
                                      const String& right) {
  if (left.length() != right.length()) return false;
  uint8_t difference = 0;
  for (size_t index = 0; index < left.length(); ++index) {
    difference |= static_cast<uint8_t>(left[index] ^ right[index]);
  }
  return difference == 0;
}

}  // namespace epd
