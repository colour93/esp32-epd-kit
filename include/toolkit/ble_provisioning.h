#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "toolkit/app.h"
#include "toolkit/config.h"
#include "toolkit/config_store.h"
#include "toolkit/display_manager.h"
#include "toolkit/ndjson_assembler.h"
#include "toolkit/network_manager.h"

namespace epd {

class ToolkitServerCallbacks;
class ToolkitCharacteristicCallbacks;

class BleProvisioningService {
 public:
  static constexpr const char* kServiceUuid =
      "f0a10000-0451-4000-b000-000000000001";
  static constexpr const char* kRxUuid =
      "f0a10001-0451-4000-b000-000000000001";
  static constexpr const char* kTxUuid =
      "f0a10002-0451-4000-b000-000000000001";

  BleProvisioningService(ConfigStore& store, NetworkManager& network,
                         AppRegistry& registry, DisplayManager& display);
  ~BleProvisioningService();

  bool begin(const DeviceConfig& current, uint8_t battery_percent,
             uint32_t passkey, uint32_t advertising_seconds);
  void loop();
  void stop();

  bool active() const { return active_; }
  bool configCommitted() const { return config_committed_; }
  bool refreshRequested() const { return refresh_requested_; }
  bool factoryResetRequested() const { return factory_reset_requested_; }
  const DeviceConfig& currentConfig() const { return current_; }

 private:
  friend class ToolkitServerCallbacks;
  friend class ToolkitCharacteristicCallbacks;

  static constexpr size_t kMaxMessageBytes = 8192;
  static constexpr uint32_t kAssemblyTimeoutMs = 5000;
  static constexpr uint32_t kHardSessionMs = 10U * 60U * 1000U;

  void onConnect(NimBLEConnInfo& connection);
  void onDisconnect(NimBLEConnInfo& connection, int reason);
  void onAuthenticationComplete(NimBLEConnInfo& connection);
  void onMtuChange(uint16_t mtu);
  void onWrite(NimBLECharacteristic* characteristic,
               NimBLEConnInfo& connection);
  void processLine(const String& line);
  void sendDocument(JsonDocument& document);
  void sendError(uint32_t id, const char* code, const String& message);
  JsonObject beginSuccess(JsonDocument& document, uint32_t id);
  bool ensureFactoryButtonConfirmation();
  String deviceName() const;

  ConfigStore& store_;
  NetworkManager& network_;
  AppRegistry& registry_;
  DisplayManager& display_;
  DeviceConfig current_;
  DeviceConfig staged_;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* rx_ = nullptr;
  NimBLECharacteristic* tx_ = nullptr;
  ToolkitServerCallbacks* server_callbacks_ = nullptr;
  ToolkitCharacteristicCallbacks* characteristic_callbacks_ = nullptr;
  core::NdjsonAssembler assembler_{kMaxMessageBytes, kAssemblyTimeoutMs};
  uint32_t session_started_at_ = 0;
  uint32_t advertising_deadline_ = 0;
  uint32_t last_activity_at_ = 0;
  uint32_t passkey_ = 0;
  uint32_t factory_nonce_ = 0;
  uint32_t factory_nonce_expires_at_ = 0;
  uint32_t factory_button_started_at_ = 0;
  uint16_t mtu_ = 23;
  uint16_t connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
  bool active_ = false;
  bool connected_ = false;
  bool authenticated_ = false;
  bool config_committed_ = false;
  bool refresh_requested_ = false;
  bool factory_reset_requested_ = false;
  bool factory_button_confirmed_ = false;
  bool request_in_progress_ = false;
};

}  // namespace epd
