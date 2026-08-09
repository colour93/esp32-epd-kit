#include "toolkit/ble_provisioning.h"

#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>

#include "toolkit/hardware.h"
#include "toolkit/usage_snapshot_store.h"

namespace epd {

constexpr size_t BleProvisioningService::kMaxMessageBytes;
constexpr uint32_t BleProvisioningService::kAssemblyTimeoutMs;
constexpr uint32_t BleProvisioningService::kHardSessionMs;

class ToolkitServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ToolkitServerCallbacks(BleProvisioningService& owner) : owner_(owner) {}

  void onConnect(NimBLEServer*, NimBLEConnInfo& connection) override {
    owner_.onConnect(connection);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& connection, int reason) override {
    owner_.onDisconnect(connection, reason);
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { owner_.onMtuChange(mtu); }
  uint32_t onPassKeyDisplay() override { return owner_.passkey_; }
  void onAuthenticationComplete(NimBLEConnInfo& connection) override {
    owner_.onAuthenticationComplete(connection);
  }

 private:
  BleProvisioningService& owner_;
};

class ToolkitCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit ToolkitCharacteristicCallbacks(BleProvisioningService& owner)
      : owner_(owner) {}

  void onWrite(NimBLECharacteristic* characteristic,
               NimBLEConnInfo& connection) override {
    owner_.onWrite(characteristic, connection);
  }

 private:
  BleProvisioningService& owner_;
};

BleProvisioningService::BleProvisioningService(ConfigStore& store,
                                               NetworkManager& network,
                                               AppRegistry& registry,
                                               DisplayManager& display)
    : store_(store),
      network_(network),
      registry_(registry),
      display_(display) {}

BleProvisioningService::~BleProvisioningService() { stop(); }

String BleProvisioningService::deviceName() const {
  const uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lX",
           static_cast<unsigned long>(mac & 0xFFFFFFULL));
  return "EPD-KIT-" + String(suffix);
}

bool BleProvisioningService::begin(const DeviceConfig& current,
                                   uint8_t battery_percent, uint32_t passkey,
                                   uint32_t advertising_seconds) {
  if (active_) return true;
  current_ = current;
  staged_ = current;
  passkey_ = passkey;
  config_committed_ = false;
  refresh_requested_ = false;
  factory_reset_requested_ = false;
  factory_button_confirmed_ = false;
  assembler_.clear();
  mtu_ = 23;

  const String name = deviceName();
  NimBLEDevice::init(name.c_str());
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityPasskey(passkey_);

  server_callbacks_ = new ToolkitServerCallbacks(*this);
  characteristic_callbacks_ = new ToolkitCharacteristicCallbacks(*this);
  server_ = NimBLEDevice::createServer();
  // BleProvisioningService owns its callback objects. NimBLE otherwise deletes
  // the server callbacks during deinit(true), causing stop() to free it twice.
  server_->setCallbacks(server_callbacks_, false);

  NimBLEService* device_information = server_->createService("180A");
  device_information->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)
      ->setValue("Waveshare / esp32-epd-kit");
  device_information->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)
      ->setValue("2.13inch e-Paper Cloud Module");
  device_information->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)
      ->setValue(EPD_TOOLKIT_VERSION);
  device_information->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)
      ->setValue(name.c_str());
  NimBLEService* battery = server_->createService("180F");
  NimBLECharacteristic* battery_level = battery->createCharacteristic(
      "2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  battery_level->setValue(&battery_percent, sizeof(battery_percent));
  NimBLEService* toolkit = server_->createService(kServiceUuid);
  rx_ = toolkit->createCharacteristic(
      kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
                   NIMBLE_PROPERTY::WRITE_AUTHEN,
      512);
  tx_ = toolkit->createCharacteristic(
      kTxUuid, NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::READ_ENC |
                   NIMBLE_PROPERTY::READ_AUTHEN,
      512);
  rx_->setCallbacks(characteristic_callbacks_);
  if (!server_->start()) {
    stop();
    return false;
  }

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->clearData();
  advertising->enableScanResponse(true);
  // A legacy BLE advertising packet has only 31 bytes. Put the 14-byte device
  // name in the scan response and keep the 128-bit Toolkit UUID in the primary
  // advertising packet. enableScanResponse() must precede setName().
  if (!advertising->setName(name.c_str()) ||
      !advertising->addServiceUUID(kServiceUuid)) {
    stop();
    return false;
  }
  if (!advertising->start()) {
    stop();
    return false;
  }

  active_ = true;
  connected_ = false;
  authenticated_ = false;
  session_started_at_ = millis();
  last_activity_at_ = session_started_at_;
  advertising_deadline_ = session_started_at_ + advertising_seconds * 1000U;
  return true;
}

void BleProvisioningService::onConnect(NimBLEConnInfo& connection) {
  connected_ = true;
  authenticated_ = connection.isEncrypted();
  connection_handle_ = connection.getConnHandle();
  staged_ = current_;
  assembler_.clear();
  last_activity_at_ = millis();
  if (server_) server_->updateConnParams(connection_handle_, 24, 48, 0, 180);
}

void BleProvisioningService::onDisconnect(NimBLEConnInfo&, int) {
  connected_ = false;
  authenticated_ = false;
  connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
  assembler_.clear();
  staged_ = current_;
  if (active_ && millis() < advertising_deadline_) NimBLEDevice::startAdvertising();
}

void BleProvisioningService::onAuthenticationComplete(
    NimBLEConnInfo& connection) {
  authenticated_ = connection.isEncrypted() && connection.isAuthenticated();
  if (!authenticated_ && server_) server_->disconnect(connection.getConnHandle());
}

void BleProvisioningService::onMtuChange(uint16_t mtu) { mtu_ = mtu; }

void BleProvisioningService::onWrite(NimBLECharacteristic* characteristic,
                                     NimBLEConnInfo& connection) {
  if (characteristic != rx_ || !connection.isEncrypted()) return;
  const std::string value = characteristic->getValue();
  if (value.empty()) return;
  last_activity_at_ = millis();
  const uint32_t hard_deadline = session_started_at_ + kHardSessionMs;
  const uint32_t idle_deadline =
      last_activity_at_ + current_.power.ble_window_sec * 1000U;
  advertising_deadline_ = std::min(hard_deadline, idle_deadline);

  const core::NdjsonFeedResult assembled = assembler_.feed(
      reinterpret_cast<const uint8_t*>(value.data()), value.size(),
      last_activity_at_);
  if (assembled.status == core::NdjsonFeedStatus::kTimeout) {
    sendError(0, "timeout", "message assembly timed out");
    return;
  }
  if (assembled.status == core::NdjsonFeedStatus::kTooLarge) {
    sendError(0, "too_large", "message exceeds 8192 bytes");
    return;
  }
  for (const std::string& assembled_line : assembled.lines) {
    if (request_in_progress_) {
      assembler_.clear();
      sendError(0, "busy", "one request is already being processed");
      return;
    }
    request_in_progress_ = true;
    processLine(String(assembled_line.c_str()));
    request_in_progress_ = false;
  }
}

JsonObject BleProvisioningService::beginSuccess(JsonDocument& document,
                                                uint32_t id) {
  document["v"] = 1;
  document["id"] = id;
  document["ok"] = true;
  return document["result"].to<JsonObject>();
}

void BleProvisioningService::sendDocument(JsonDocument& document) {
  if (!connected_ || tx_ == nullptr) return;
  String payload;
  serializeJson(document, payload);
  payload += '\n';
  const size_t chunk_size =
      std::max<size_t>(20, std::min<size_t>(244, mtu_ > 3 ? mtu_ - 3 : 20));
  for (size_t offset = 0; offset < payload.length(); offset += chunk_size) {
    const size_t length = std::min(chunk_size, payload.length() - offset);
    if (!tx_->indicate(reinterpret_cast<const uint8_t*>(payload.c_str() + offset),
                       length, connection_handle_)) {
      break;
    }
    delay(12);
  }
}

void BleProvisioningService::sendError(uint32_t id, const char* code,
                                       const String& message) {
  JsonDocument response;
  response["v"] = 1;
  response["id"] = id;
  response["ok"] = false;
  JsonObject error = response["error"].to<JsonObject>();
  error["code"] = code;
  error["message"] = message;
  sendDocument(response);
}

void BleProvisioningService::processLine(const String& line) {
  JsonDocument request;
  const DeserializationError parse_error = deserializeJson(request, line);
  if (parse_error) {
    sendError(0, "invalid_request", String("invalid JSON: ") + parse_error.c_str());
    return;
  }
  if (!request.is<JsonObject>()) {
    sendError(0, "invalid_request", "request must be a JSON object");
    return;
  }
  if (!request["id"].is<uint32_t>()) {
    sendError(0, "invalid_request", "id must be a uint32");
    return;
  }
  const uint32_t id = request["id"].as<uint32_t>();
  if (!request["v"].is<uint8_t>()) {
    sendError(id, "invalid_request", "v must be an integer");
    return;
  }
  if (request["v"].as<uint8_t>() != 1) {
    sendError(id, "unsupported_version", "only protocol v1 is supported");
    return;
  }
  if (!request["op"].is<const char*>()) {
    sendError(id, "invalid_request", "op is required");
    return;
  }
  const String operation = request["op"].as<const char*>();
  JsonVariantConst args = request["args"];
  if (!args.is<JsonObjectConst>()) {
    sendError(id, "invalid_request", "args must be an object");
    return;
  }

  if (operation == "hello") {
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["protocol"] = 1;
    result["firmware"] = EPD_TOOLKIT_VERSION;
    result["device_name"] = deviceName();
    result["max_message_bytes"] = kMaxMessageBytes;
    result["mtu"] = mtu_;
    result["security"] = authenticated_ ? "encrypted_mitm" : "encrypted";
    sendDocument(response);
    return;
  }

  if (operation == "device.status") {
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["configured"] = current_.isConfigured();
    result["config_committed"] = config_committed_;
    result["active_app"] = current_.active_app;
    result["uptime_ms"] = millis();
    result["wifi_ssid"] = current_.wifi.ssid;
    sendDocument(response);
    return;
  }

  if (operation == "config.get") {
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    configToJson(current_, result["config"].to<JsonObject>(), true);
    sendDocument(response);
    return;
  }

  if (operation == "config.patch") {
    JsonVariantConst patch = args["patch"];
    String error;
    DeviceConfig candidate = staged_;
    if (!applyConfigPatch(patch, candidate, error)) {
      sendError(id, "invalid_config", error);
      return;
    }
    staged_ = candidate;
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["staged"] = true;
    result["configured"] = staged_.isConfigured();
    sendDocument(response);
    return;
  }

  if (operation == "config.commit") {
    String error;
    if (!store_.save(staged_, error)) {
      sendError(id, "internal_error", error);
      return;
    }
    current_ = staged_;
    config_committed_ = true;
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["committed"] = true;
    result["configured"] = current_.isConfigured();
    sendDocument(response);
    return;
  }

  if (operation == "wifi.scan") {
    WiFi.mode(WIFI_STA);
    const int count = WiFi.scanNetworks();
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    JsonArray networks = result["networks"].to<JsonArray>();
    for (int i = 0; i < count && i < 10; ++i) {
      JsonObject network = networks.add<JsonObject>();
      network["ssid"] = WiFi.SSID(i);
      network["rssi"] = WiFi.RSSI(i);
      network["channel"] = WiFi.channel(i);
      network["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    sendDocument(response);
    return;
  }

  if (operation == "wifi.test") {
    const bool connected = network_.connect(staged_.wifi, 12000);
    const int rssi = connected ? WiFi.RSSI() : 0;
    const String error = network_.lastError();
    const String local_ip = connected ? WiFi.localIP().toString() : "";
    const String gateway = connected ? WiFi.gatewayIP().toString() : "";
    const String subnet = connected ? WiFi.subnetMask().toString() : "";
    const String dns1 = connected ? WiFi.dnsIP(0).toString() : "";
    const String dns2 = connected ? WiFi.dnsIP(1).toString() : "";
    network_.disconnect();
    if (!connected) {
      sendError(id, "wifi_failed", error);
      return;
    }
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["connected"] = true;
    result["rssi"] = rssi;
    result["ipv4_mode"] = staged_.wifi.ipv4.mode == WifiIpv4Mode::kStatic
                              ? "static"
                              : "dhcp";
    result["ip"] = local_ip;
    result["gateway"] = gateway;
    result["subnet"] = subnet;
    result["dns1"] = dns1;
    result["dns2"] = dns2;
    sendDocument(response);
    return;
  }

  if (operation == "app.list") {
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    JsonArray apps = result["apps"].to<JsonArray>();
    for (size_t index = 0; index < registry_.size(); ++index) {
      const AppManifest manifest = registry_.at(index).manifest();
      JsonObject app = apps.add<JsonObject>();
      app["id"] = manifest.id;
      app["name"] = manifest.name;
      app["version"] = manifest.version;
      app["active"] = current_.active_app == manifest.id;
    }
    sendDocument(response);
    return;
  }

  if (operation == "app.activate") {
    const String app_id = args["id"] | "";
    if (registry_.find(app_id) == nullptr) {
      sendError(id, "invalid_config", "unknown app id");
      return;
    }
    staged_.active_app = app_id;
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["staged"] = true;
    result["active_app"] = app_id;
    sendDocument(response);
    return;
  }

  if (operation == "refresh.now") {
    refresh_requested_ = true;
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["scheduled"] = true;
    sendDocument(response);
    return;
  }

  if (operation == "factory_reset.prepare") {
    do {
      factory_nonce_ = esp_random();
    } while (factory_nonce_ == 0);
    factory_nonce_expires_at_ = millis() + 30000U;
    factory_button_started_at_ = 0;
    factory_button_confirmed_ = false;
    display_.renderFactoryResetConfirmation();
    display_.present(current_.display);
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["nonce"] = factory_nonce_;
    result["expires_in_sec"] = 30;
    result["physical_confirmation_required"] = true;
    sendDocument(response);
    return;
  }

  if (operation == "factory_reset.commit") {
    const uint32_t nonce = args["nonce"] | 0U;
    if (nonce == 0 || nonce != factory_nonce_ ||
        millis() > factory_nonce_expires_at_) {
      sendError(id, "unauthorized", "factory-reset nonce is invalid or expired");
      return;
    }
    if (!factory_button_confirmed_) {
      sendError(id, "unauthorized", "hold the physical KEY for two seconds");
      return;
    }
    String error;
    if (!store_.erase(error)) {
      sendError(id, "internal_error", error);
      return;
    }
    UsageSnapshotStore snapshot_store;
    if (!snapshot_store.erase(error)) {
      sendError(id, "internal_error", error);
      return;
    }
    NimBLEDevice::deleteAllBonds();
    factory_reset_requested_ = true;
    JsonDocument response;
    JsonObject result = beginSuccess(response, id);
    result["reset"] = true;
    sendDocument(response);
    return;
  }

  sendError(id, "invalid_request", "unsupported operation");
}

bool BleProvisioningService::ensureFactoryButtonConfirmation() {
  if (factory_nonce_ == 0 || millis() > factory_nonce_expires_at_) {
    factory_button_started_at_ = 0;
    return false;
  }
  if (digitalRead(hardware::kUserKey) != LOW) {
    factory_button_started_at_ = 0;
    return factory_button_confirmed_;
  }
  if (factory_button_started_at_ == 0) factory_button_started_at_ = millis();
  if (millis() - factory_button_started_at_ >= 2000U) {
    factory_button_confirmed_ = true;
  }
  return factory_button_confirmed_;
}

void BleProvisioningService::loop() {
  if (!active_) return;
  ensureFactoryButtonConfirmation();
  const uint32_t now = millis();
  if (assembler_.expire(now)) {
    sendError(0, "timeout", "message assembly timed out");
  }
  if (connected_ && now - last_activity_at_ > current_.power.ble_window_sec * 1000U) {
    if (server_ && connection_handle_ != BLE_HS_CONN_HANDLE_NONE) {
      server_->disconnect(connection_handle_);
    }
  }
  if (now - session_started_at_ > kHardSessionMs ||
      (!connected_ && now > advertising_deadline_)) {
    stop();
  }
}

void BleProvisioningService::stop() {
  if (!active_ && server_ == nullptr) return;
  active_ = false;
  NimBLEDevice::stopAdvertising();
  if (server_ && connection_handle_ != BLE_HS_CONN_HANDLE_NONE) {
    server_->disconnect(connection_handle_);
  }
  NimBLEDevice::deinit(true);
  server_ = nullptr;
  rx_ = nullptr;
  tx_ = nullptr;
  connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
  delete server_callbacks_;
  delete characteristic_callbacks_;
  server_callbacks_ = nullptr;
  characteristic_callbacks_ = nullptr;
  connected_ = false;
  authenticated_ = false;
}

}  // namespace epd
