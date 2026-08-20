#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#include "toolkit/app.h"
#include "toolkit/config.h"
#include "toolkit/config_store.h"
#include "toolkit/display_manager.h"
#include "toolkit/resource_store.h"

namespace epd {

class ToolkitServerCallbacks;
class ToolkitCharacteristicCallbacks;

enum class ProtocolTransportKind : uint8_t { kBle = 0, kLan = 1 };

class ProtocolTransport {
 public:
  virtual ~ProtocolTransport() = default;
  virtual const char* name() const = 0;
  virtual bool connected() const = 0;
  virtual bool authenticated() const = 0;
  virtual bool owner() const = 0;
  virtual size_t frameBytes() const = 0;
  virtual bool sendFrame(const uint8_t* data, size_t length) = 0;
  virtual void disconnect() = 0;
};

class BleProtocolService {
 public:
  static constexpr const char* kServiceUuid =
      "f0a40000-0451-4000-b000-000000000001";
  static constexpr const char* kRxUuid =
      "f0a40001-0451-4000-b000-000000000001";
  static constexpr const char* kTxUuid =
      "f0a40002-0451-4000-b000-000000000001";
  static constexpr uint8_t kProtocolMajor = 4;
  static constexpr uint8_t kProtocolMinor = 1;

  BleProtocolService(ConfigStore& config_store, ResourceStore& resources,
                     PageRegistry& pages, DisplayManager& display);
  ~BleProtocolService();

  bool begin(const DeviceConfig& config, uint16_t battery_mv,
             uint32_t passkey);
  void loop();
  void stop();
  void emitKeyPressed();
  void updateBattery(uint16_t battery_mv);
  void emitDisplayStarted(bool full);
  void emitDisplayCompleted(const char* result);
  void attachTransport(ProtocolTransport& transport);
  void transportConnected(ProtocolTransport& transport);
  void transportDisconnected(ProtocolTransport& transport);
  void receiveTransportFrame(ProtocolTransport& transport,
                             const uint8_t* data, size_t length);

  bool connected() const;
  bool authenticated() const;
  bool sessionReady() const { return connected() && authenticated(); }
  bool owned() const { return !owner_address_.isEmpty(); }
  bool takeRenderRequest(bool& force_full);
  bool takeRestartRequest();
  bool takeFactoryResetRequest();
  bool takeFactoryCode(uint32_t& code);
  bool takeSleepRequest();
  bool factoryReset(String& error);
  bool enterSetupMode(String& error);
  bool setupMode() const;
  uint32_t setupRemainingSeconds() const;
  uint32_t passkey() const { return passkey_; }
  int16_t utcOffsetMinutes() const { return utc_offset_minutes_; }
  const DeviceConfig& config() const { return current_; }

 private:
  friend class ToolkitServerCallbacks;
  friend class ToolkitCharacteristicCallbacks;

  enum class MessageKind : uint8_t { kRequest = 0, kResponse = 1, kEvent = 2 };

  struct Assembly {
    bool active = false;
    uint32_t id = 0;
    uint16_t next_sequence = 0;
    uint16_t total_length = 0;
    uint32_t expected_crc = 0;
    uint32_t started_at = 0;
    std::vector<uint8_t> payload;

    void clear() {
      active = false;
      id = 0;
      next_sequence = 0;
      total_length = 0;
      expected_crc = 0;
      started_at = 0;
      payload.clear();
    }
  };

  struct PendingRequest {
    uint32_t id = 0;
    uint32_t connection_generation = 0;
    ProtocolTransportKind transport = ProtocolTransportKind::kBle;
    std::vector<uint8_t> payload;
    const char* error_code = nullptr;
    const char* error_message = nullptr;
    bool retryable = false;
  };

  static constexpr uint8_t kFrameMagic = 0xE4;
  static constexpr uint8_t kFlagKindMask = 0x03;
  static constexpr uint8_t kFlagStart = 0x04;
  static constexpr uint8_t kFlagEnd = 0x08;
  static constexpr size_t kFrameHeaderBytes = 8;
  static constexpr size_t kStartMetadataBytes = 6;
  static constexpr size_t kMaxMessageBytes = 8192;
  static constexpr uint32_t kAssemblyTimeoutMs = 5000;
  static constexpr size_t kMaxPendingRequests = 4;
  static constexpr uint32_t kTxRetryDelayMs = 20;
  static constexpr uint8_t kTxMaxSendAttempts = 3;
  static constexpr uint32_t kEnrollmentWindowMs = 120000;
  static constexpr uint32_t kBatteryAwakeTimeoutMs = 120000;
  static constexpr uint32_t kAdvertisingRestartDelayMs = 250;
  static constexpr uint32_t kAdvertisingRetryDelayMs = 1000;

  void onConnect(NimBLEConnInfo& connection);
  void onDisconnect(NimBLEConnInfo& connection, int reason);
  void onAuthenticationComplete(NimBLEConnInfo& connection);
  void onIdentity(NimBLEConnInfo& connection);
  void onMtuChange(uint16_t mtu, NimBLEConnInfo& connection);
  void onIndicationStatus(int status);
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connection);
  void acceptFrame(ProtocolTransportKind transport,
                   uint32_t connection_generation, Assembly& assembly,
                   const uint8_t* data, size_t length);
  void processRequest(uint32_t id, const uint8_t* payload, size_t length);
  void sendDocument(uint32_t id, MessageKind kind, JsonDocument& document);
  void sendDocumentTo(ProtocolTransportKind transport, uint32_t id,
                      MessageKind kind, JsonDocument& document);
  void sendError(uint32_t id, const char* code, const String& message,
                 bool retryable = false);
  void sendEvent(const char* name, JsonVariantConst data = JsonVariantConst());
  void sendSimpleEvent(const char* name);
  void queueErrorLocked(ProtocolTransportKind transport,
                        uint32_t connection_generation, uint32_t id,
                        const char* code, const char* message,
                        bool retryable = false);
  void processPendingRequest();
  void refreshAuthenticatedPeer();
  void pumpTx();
  bool requireOwner(uint32_t id);
  bool requireTrusted(uint32_t id);
  bool isOwner() const;
  bool isKnownBond(const String& address) const;
  void loadSecurityState();
  void saveOwner(const NimBLEAddress& address);
  void clearSecurityState();
  void closeSetupMode();
  String bondId(const NimBLEAddress& address) const;
  bool findBond(const String& id, NimBLEAddress& address) const;
  String deviceName() const;
  bool configureAdvertising(bool fast);
  static uint32_t crc32(const uint8_t* data, size_t length);
  static uint16_t readU16(const uint8_t* value);
  static uint32_t readU32(const uint8_t* value);
  static void writeU16(uint8_t* target, uint16_t value);
  static void writeU32(uint8_t* target, uint32_t value);

  ConfigStore& config_store_;
  ResourceStore& resources_;
  PageRegistry& pages_;
  DisplayManager& display_;
  DeviceConfig current_;
  DeviceConfig staged_;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* rx_ = nullptr;
  NimBLECharacteristic* tx_ = nullptr;
  NimBLECharacteristic* battery_level_ = nullptr;
  ToolkitServerCallbacks* server_callbacks_ = nullptr;
  ToolkitCharacteristicCallbacks* characteristic_callbacks_ = nullptr;
  Assembly assembly_;
  Assembly external_assembly_;
  std::mutex rx_mutex_;
  std::deque<PendingRequest> pending_requests_;
  std::mutex tx_mutex_;
  std::deque<std::vector<uint8_t>> tx_frames_;
  std::vector<String> known_bonds_;
  String owner_address_;
  String peer_address_;
  uint32_t passkey_ = 0;
  uint32_t enrollment_deadline_ = 0;
  uint32_t factory_code_ = 0;
  uint32_t factory_deadline_ = 0;
  uint32_t next_event_id_ = 1;
  uint32_t fast_advertising_deadline_ = 0;
  uint32_t advertising_restart_at_ = 0;
  uint32_t battery_awake_deadline_ = 0;
  uint32_t tx_retry_at_ = 0;
  uint32_t connection_generation_ = 0;
  uint32_t request_connection_generation_ = 0;
  uint32_t external_connection_generation_ = 0;
  uint16_t mtu_ = 23;
  uint16_t connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
  int16_t utc_offset_minutes_ = 0;
  bool active_ = false;
  std::atomic_bool connected_{false};
  std::atomic_bool authenticated_{false};
  std::atomic_bool connection_render_requested_{false};
  std::atomic_bool setup_render_requested_{false};
  bool request_in_progress_ = false;
  ProtocolTransportKind request_transport_ = ProtocolTransportKind::kBle;
  ProtocolTransport* external_transport_ = nullptr;
  bool tx_waiting_for_ack_ = false;
  uint8_t tx_send_attempts_ = 0;
  bool render_requested_ = false;
  bool force_full_requested_ = false;
  bool restart_requested_ = false;
  bool factory_reset_requested_ = false;
  bool factory_code_pending_ = false;
  bool sleep_requested_ = false;
  uint32_t page_freshness_signature_ = 0;
};

}  // namespace epd
