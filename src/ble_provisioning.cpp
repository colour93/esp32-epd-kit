#include "toolkit/ble_provisioning.h"

#include <Preferences.h>
#include <esp_system.h>
#include <sys/time.h>

#include <algorithm>
#include <string>

#include "toolkit/log.h"

namespace epd {

namespace {

bool deadlineReached(uint32_t deadline) {
  return deadline != 0 &&
         static_cast<int32_t>(millis() - deadline) >= 0;
}

uint64_t unixNow() {
  const time_t value = time(nullptr);
  return value > 0 ? static_cast<uint64_t>(value) : 0;
}

}  // namespace

class ToolkitServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ToolkitServerCallbacks(BleProtocolService& owner) : owner_(owner) {}
  void onConnect(NimBLEServer*, NimBLEConnInfo& connection) override {
    owner_.onConnect(connection);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& connection,
                    int reason) override {
    owner_.onDisconnect(connection, reason);
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& connection) override {
    owner_.onMtuChange(mtu, connection);
  }
  uint32_t onPassKeyDisplay() override { return owner_.passkey_; }
  void onAuthenticationComplete(NimBLEConnInfo& connection) override {
    owner_.onAuthenticationComplete(connection);
  }
  void onIdentity(NimBLEConnInfo& connection) override {
    owner_.onIdentity(connection);
  }

 private:
  BleProtocolService& owner_;
};

class ToolkitCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit ToolkitCharacteristicCallbacks(BleProtocolService& owner)
      : owner_(owner) {}
  void onWrite(NimBLECharacteristic* characteristic,
               NimBLEConnInfo& connection) override {
    owner_.onWrite(characteristic, connection);
  }
  void onStatus(NimBLECharacteristic*, int status) override {
    owner_.onIndicationStatus(status);
  }

 private:
  BleProtocolService& owner_;
};

BleProtocolService::BleProtocolService(ConfigStore& config_store,
                                       ResourceStore& resources,
                                       PageRegistry& pages,
                                       DisplayManager& display)
    : config_store_(config_store),
      resources_(resources),
      pages_(pages),
      display_(display) {}

BleProtocolService::~BleProtocolService() { stop(); }

uint16_t BleProtocolService::readU16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(value[1]) << 8U;
}

uint32_t BleProtocolService::readU32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         static_cast<uint32_t>(value[1]) << 8U |
         static_cast<uint32_t>(value[2]) << 16U |
         static_cast<uint32_t>(value[3]) << 24U;
}

void BleProtocolService::writeU16(uint8_t* target, uint16_t value) {
  target[0] = value & 0xFFU;
  target[1] = value >> 8U;
}

void BleProtocolService::writeU32(uint8_t* target, uint32_t value) {
  target[0] = value & 0xFFU;
  target[1] = (value >> 8U) & 0xFFU;
  target[2] = (value >> 16U) & 0xFFU;
  target[3] = (value >> 24U) & 0xFFU;
}

uint32_t BleProtocolService::crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

String BleProtocolService::deviceName() const {
  const uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lX",
           static_cast<unsigned long>(mac & 0xFFFFFFULL));
  return "EPD-KIT-" + String(suffix);
}

void BleProtocolService::loadSecurityState() {
  known_bonds_.clear();
  for (int index = 0; index < NimBLEDevice::getNumBonds(); ++index) {
    known_bonds_.push_back(
        String(NimBLEDevice::getBondedAddress(index).toString().c_str()));
  }
  Preferences preferences;
  if (preferences.begin("epd_sec4", false)) {
    owner_address_ = preferences.getString("owner", "");
    if (!owner_address_.isEmpty() && !isKnownBond(owner_address_)) {
      owner_address_ = "";
      preferences.remove("owner");
      TOOLKIT_LOG("security",
                  "owner bond missing; cleared orphaned owner state");
    }
    preferences.end();
  }
}

void BleProtocolService::saveOwner(const NimBLEAddress& address) {
  owner_address_ = address.toString().c_str();
  Preferences preferences;
  if (preferences.begin("epd_sec4", false)) {
    preferences.putString("owner", owner_address_);
    preferences.end();
  }
}

void BleProtocolService::clearSecurityState() {
  owner_address_ = "";
  known_bonds_.clear();
  Preferences preferences;
  if (preferences.begin("epd_sec4", false)) {
    preferences.clear();
    preferences.end();
  }
}

bool BleProtocolService::isKnownBond(const String& address) const {
  for (const String& known : known_bonds_) {
    if (known == address) return true;
  }
  return false;
}

bool BleProtocolService::isOwner() const {
  return authenticated_ && !owner_address_.isEmpty() &&
         peer_address_ == owner_address_;
}

String BleProtocolService::bondId(const NimBLEAddress& address) const {
  const std::string text = address.toString();
  const uint32_t value = crc32(reinterpret_cast<const uint8_t*>(text.data()),
                               text.size());
  char id[12];
  snprintf(id, sizeof(id), "b-%08lx", static_cast<unsigned long>(value));
  return String(id);
}

bool BleProtocolService::findBond(const String& id,
                                  NimBLEAddress& address) const {
  for (int index = 0; index < NimBLEDevice::getNumBonds(); ++index) {
    const NimBLEAddress candidate = NimBLEDevice::getBondedAddress(index);
    if (bondId(candidate) == id) {
      address = candidate;
      return true;
    }
  }
  return false;
}

bool BleProtocolService::configureAdvertising(bool fast) {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising == nullptr) return false;
  std::vector<uint8_t> manufacturer{
      0xFFU, 0xFFU,  // Internal-use Company ID.
      kProtocolMajor,
      static_cast<uint8_t>((owner_address_.isEmpty() ? 0U : 0x01U) |
                           (current_.hardware.battery.enabled ? 0x02U : 0U) |
                           (current_.hardware.io12_mode == Io12Mode::kKey
                                ? 0x04U
                                : 0U) |
                           (fast ? 0x08U : 0U) |
                           (setupMode() ? 0x10U : 0U))};
  advertising->clearData();
  advertising->enableScanResponse(true);
  if (!advertising->setName(deviceName().c_str()) ||
      !advertising->addServiceUUID(kServiceUuid) ||
      !advertising->setManufacturerData(manufacturer)) {
    TOOLKIT_LOG("ble", "advertising data setup failed");
    return false;
  }
  if (fast || current_.power.profile == PowerProfile::kMains) {
    advertising->setMinInterval(160);   // 100 ms
    advertising->setMaxInterval(240);   // 150 ms
  } else {
    advertising->setMinInterval(1440);  // 900 ms
    advertising->setMaxInterval(1760);  // 1100 ms
  }
  TOOLKIT_LOG("ble", String("advertising profile=") +
                         (fast || current_.power.profile == PowerProfile::kMains
                              ? "fast"
                              : "low-duty"));
  return true;
}

bool BleProtocolService::begin(const DeviceConfig& config, uint16_t battery_mv,
                               uint32_t passkey) {
  if (active_) return true;
  current_ = config;
  staged_ = config;
  passkey_ = passkey;
  const uint64_t now = unixNow();
  IPage* active_page = pages_.find(current_.page.id);
  page_freshness_signature_ =
      active_page == nullptr
          ? 0
          : PageResources(active_page->manifest(), current_.page, resources_, now)
                .freshnessSignature();
  const String name = deviceName();
  NimBLEDevice::init(name.c_str());
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityPasskey(passkey_);
  loadSecurityState();
  if (current_.power.profile == PowerProfile::kBattery &&
      !owner_address_.isEmpty()) {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    battery_awake_deadline_ = millis() + kBatteryAwakeTimeoutMs;
  }
  TOOLKIT_LOG("ble", String("initializing name=") + name +
                         " owned=" + (!owner_address_.isEmpty() ? "yes" : "no") +
                         " bonds=" + known_bonds_.size());

  server_callbacks_ = new ToolkitServerCallbacks(*this);
  characteristic_callbacks_ = new ToolkitCharacteristicCallbacks(*this);
  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(server_callbacks_, false);

  NimBLEService* information = server_->createService("180A");
  information->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)
      ->setValue("Waveshare / esp32-epd-kit");
  information->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)
      ->setValue("2.13inch e-Paper Cloud Module");
  information->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)
      ->setValue(EPD_TOOLKIT_VERSION);
  information->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)
      ->setValue(name.c_str());

  if (current_.hardware.battery.enabled) {
    NimBLEService* battery = server_->createService("180F");
    battery_level_ = battery->createCharacteristic(
        "2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    uint8_t percent = battery_mv <= 3300
                          ? 0
                          : battery_mv >= 4200
                                ? 100
                                : static_cast<uint8_t>((battery_mv - 3300U) /
                                                       9U);
    battery_level_->setValue(&percent, sizeof(percent));
  }

  NimBLEService* service = server_->createService(kServiceUuid);
  rx_ = service->createCharacteristic(
      kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
                   NIMBLE_PROPERTY::WRITE_AUTHEN,
      512);
  tx_ = service->createCharacteristic(
      kTxUuid, NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::READ_ENC |
                   NIMBLE_PROPERTY::READ_AUTHEN,
      512);
  rx_->setCallbacks(characteristic_callbacks_);
  tx_->setCallbacks(characteristic_callbacks_);
  if (!server_->start()) {
    TOOLKIT_LOG("ble", "GATT server start failed");
    stop();
    return false;
  }

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!configureAdvertising(true)) {
    stop();
    return false;
  }
  if (!advertising->start()) {
    TOOLKIT_LOG("ble", "advertising start failed");
    stop();
    return false;
  }
  fast_advertising_deadline_ = millis() + 30000U;
  active_ = true;
  TOOLKIT_LOG("ble", String("advertising started service=") + kServiceUuid);
  if (owner_address_.isEmpty()) {
    char formatted_passkey[7];
    snprintf(formatted_passkey, sizeof(formatted_passkey), "%06lu",
             static_cast<unsigned long>(passkey_));
    TOOLKIT_LOG("security", String("pairing passkey=") + formatted_passkey);
    display_.renderPairing(passkey_, false);
    display_.present(current_.display);
  }
  return true;
}

void BleProtocolService::onConnect(NimBLEConnInfo& connection) {
  const uint16_t connection_handle = connection.getConnHandle();
  authenticated_ = connection.isEncrypted() && connection.isAuthenticated();
  peer_address_ = "";
  uint16_t replaced_handle = BLE_HS_CONN_HANDLE_NONE;
  {
    const std::lock_guard<std::mutex> lock(rx_mutex_);
    assembly_.clear();
    pending_requests_.clear();
  }
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (connected_ && connection_handle_ != connection_handle) {
      replaced_handle = connection_handle_;
    }
    connected_ = true;
    advertising_restart_at_ = 0;
    connection_handle_ = connection_handle;
    mtu_ = 23;
    ++connection_generation_;
    tx_frames_.clear();
    tx_waiting_for_ack_ = false;
    tx_send_attempts_ = 0;
    tx_retry_at_ = 0;
  }
  staged_ = current_;
  TOOLKIT_LOG("ble", String("connected handle=") + connection_handle +
                         " encrypted=" + (connection.isEncrypted() ? "yes" : "no") +
                         " authenticated=" +
                         (connection.isAuthenticated() ? "yes" : "no"));
  if (server_ && replaced_handle != BLE_HS_CONN_HANDLE_NONE) {
    TOOLKIT_LOG("ble", String("replacing stale connection handle=") +
                           replaced_handle);
    server_->disconnect(replaced_handle);
  }
  if (server_) server_->updateConnParams(connection_handle, 12, 24, 0, 240);
}

void BleProtocolService::onDisconnect(NimBLEConnInfo& connection, int reason) {
  const uint16_t disconnected_handle = connection.getConnHandle();
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (connected_ && connection_handle_ != disconnected_handle) {
      TOOLKIT_LOG("ble", String("ignored stale disconnect handle=") +
                             disconnected_handle + " active=" +
                             connection_handle_ + " reason=" + reason);
      return;
    }
  }
  TOOLKIT_LOG("ble", String("disconnected handle=") + disconnected_handle +
                         " reason=" + reason);
  authenticated_ = false;
  peer_address_ = "";
  {
    const std::lock_guard<std::mutex> lock(rx_mutex_);
    assembly_.clear();
    pending_requests_.clear();
  }
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    connected_ = false;
    advertising_restart_at_ = millis() + kAdvertisingRestartDelayMs;
    fast_advertising_deadline_ = millis() + 30000U;
    connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
    mtu_ = 23;
    ++connection_generation_;
    tx_frames_.clear();
    tx_waiting_for_ack_ = false;
    tx_send_attempts_ = 0;
    tx_retry_at_ = 0;
  }
  staged_ = current_;
  if (setupMode()) {
    setup_render_requested_ = true;
  } else {
    connection_render_requested_ = !owner_address_.isEmpty();
  }
  TOOLKIT_LOG("ble", "advertising restart scheduled");
}

void BleProtocolService::onAuthenticationComplete(NimBLEConnInfo& connection) {
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || connection.getConnHandle() != connection_handle_) return;
  }
  authenticated_ = connection.isEncrypted() && connection.isAuthenticated() &&
                   connection.isBonded();
  const NimBLEAddress identity = connection.getIdAddress();
  peer_address_ = identity.toString().c_str();
  const bool peer_was_known = isKnownBond(peer_address_);
  if (!authenticated_) {
    if (setupMode()) {
      setup_render_requested_ = true;
    } else {
      connection_render_requested_ = !owner_address_.isEmpty();
    }
    TOOLKIT_LOG("security", "pairing authentication failed");
    if (server_) server_->disconnect(connection.getConnHandle());
    return;
  }
  if (owner_address_.isEmpty()) {
    saveOwner(connection.getIdAddress());
    known_bonds_.push_back(peer_address_);
    if (current_.power.profile == PowerProfile::kBattery) {
      const std::lock_guard<std::mutex> lock(tx_mutex_);
      battery_awake_deadline_ = millis() + kBatteryAwakeTimeoutMs;
    }
    render_requested_ = true;
    if (setupMode()) {
      setup_render_requested_ = true;
    } else {
      connection_render_requested_ = true;
    }
    TOOLKIT_LOG("security", "first authenticated bond assigned as owner");
    return;
  }
  if (!peer_was_known && (identity.isRpa() || identity.isNull())) {
    authenticated_ = false;
    connection_render_requested_ = true;
    TOOLKIT_LOG("security", "waiting for bonded identity resolution");
    return;
  }
  const bool enrollment_open = enrollment_deadline_ != 0 &&
                               !deadlineReached(enrollment_deadline_);
  if (!peer_was_known && !enrollment_open) {
    TOOLKIT_LOG("security", "untrusted bond rejected; enrollment closed");
    NimBLEDevice::deleteBond(connection.getIdAddress());
    if (server_) server_->disconnect(connection.getConnHandle());
    return;
  }
  if (!peer_was_known && enrollment_open) {
    if (NimBLEDevice::getNumBonds() > 4) {
      TOOLKIT_LOG("security", "trusted bond rejected; bond limit reached");
      NimBLEDevice::deleteBond(connection.getIdAddress());
      if (server_) server_->disconnect(connection.getConnHandle());
      return;
    }
    known_bonds_.push_back(peer_address_);
    TOOLKIT_LOG("security", "trusted bond enrolled");
  } else {
    TOOLKIT_LOG("security", String("authenticated role=") +
                                (isOwner() ? "owner" : "trusted"));
  }
  if (setupMode()) {
    setup_render_requested_ = true;
  } else {
    connection_render_requested_ = true;
  }
}

void BleProtocolService::onIdentity(NimBLEConnInfo& connection) {
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || connection.getConnHandle() != connection_handle_) return;
  }
  if (!connection.isEncrypted() || !connection.isAuthenticated()) return;
  const String identity = connection.getIdAddress().toString().c_str();
  if (!isKnownBond(identity)) return;
  peer_address_ = identity;
  authenticated_ = true;
  if (setupMode()) {
    setup_render_requested_ = true;
  } else {
    connection_render_requested_ = true;
  }
  TOOLKIT_LOG("security", String("resolved identity role=") +
                              (isOwner() ? "owner" : "trusted"));
}

void BleProtocolService::onMtuChange(uint16_t mtu,
                                     NimBLEConnInfo& connection) {
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || connection.getConnHandle() != connection_handle_) return;
    mtu_ = mtu;
  }
  TOOLKIT_LOG("ble", String("MTU changed to ") + mtu);
}

void BleProtocolService::onIndicationStatus(int status) {
  const std::lock_guard<std::mutex> lock(tx_mutex_);
  if (!connected_ || !tx_waiting_for_ack_) return;
  tx_waiting_for_ack_ = false;
  tx_send_attempts_ = 0;
  tx_retry_at_ = 0;
  if (status != BLE_HS_EDONE) {
    TOOLKIT_LOG("ble.tx", String("indication failed status=") + status);
    tx_frames_.clear();
  }
}

void BleProtocolService::onWrite(NimBLECharacteristic* characteristic,
                                 NimBLEConnInfo& connection) {
  if (characteristic != rx_ || !connection.isEncrypted() ||
      !connection.isAuthenticated()) {
    TOOLKIT_LOG("security", "rejected unauthenticated RX write");
    return;
  }
  if (current_.power.profile == PowerProfile::kBattery &&
      !owner_address_.isEmpty()) {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    battery_awake_deadline_ = millis() + kBatteryAwakeTimeoutMs;
    sleep_requested_ = false;
  }
  uint32_t connection_generation = 0;
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || connection.getConnHandle() != connection_handle_) return;
    connection_generation = connection_generation_;
  }
  const std::string value = characteristic->getValue();
  const std::lock_guard<std::mutex> lock(rx_mutex_);
  if (value.size() < kFrameHeaderBytes) {
    queueErrorLocked(connection_generation, 0, "invalid_frame",
                     "frame header is incomplete");
    return;
  }
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value.data());
  if (bytes[0] != kFrameMagic ||
      (bytes[1] & kFlagKindMask) != static_cast<uint8_t>(MessageKind::kRequest)) {
    queueErrorLocked(connection_generation, 0, "invalid_frame",
                     "frame magic or kind is invalid");
    return;
  }
  const uint8_t flags = bytes[1];
  const uint32_t id = readU32(bytes + 2);
  const uint16_t sequence = readU16(bytes + 6);
  size_t offset = kFrameHeaderBytes;
  const uint32_t now = millis();
  if (assembly_.active && now - assembly_.started_at > kAssemblyTimeoutMs) {
    assembly_.clear();
    queueErrorLocked(connection_generation, id, "invalid_frame",
                     "message assembly timed out", true);
    return;
  }
  if ((flags & kFlagStart) != 0) {
    if (sequence != 0 || value.size() < kFrameHeaderBytes + kStartMetadataBytes) {
      queueErrorLocked(connection_generation, id, "invalid_frame",
                       "start frame metadata is invalid");
      return;
    }
    assembly_.clear();
    assembly_.active = true;
    assembly_.id = id;
    assembly_.next_sequence = 0;
    assembly_.total_length = readU16(bytes + offset);
    assembly_.expected_crc = readU32(bytes + offset + 2);
    assembly_.started_at = now;
    offset += kStartMetadataBytes;
    if (assembly_.total_length == 0 ||
        assembly_.total_length > kMaxMessageBytes) {
      assembly_.clear();
      queueErrorLocked(connection_generation, id, "too_large",
                       "message length is outside supported limits");
      return;
    }
    assembly_.payload.reserve(assembly_.total_length);
  }
  if (!assembly_.active || assembly_.id != id ||
      sequence != assembly_.next_sequence) {
    assembly_.clear();
    queueErrorLocked(connection_generation, id, "invalid_frame",
                     "fragment sequence is not contiguous", true);
    return;
  }
  ++assembly_.next_sequence;
  const size_t chunk_length = value.size() - offset;
  if (assembly_.payload.size() + chunk_length > assembly_.total_length) {
    assembly_.clear();
    queueErrorLocked(connection_generation, id, "too_large",
                     "fragment exceeds declared message length");
    return;
  }
  assembly_.payload.insert(assembly_.payload.end(), bytes + offset,
                           bytes + value.size());
  if ((flags & kFlagEnd) == 0) return;
  if (assembly_.payload.size() != assembly_.total_length ||
      crc32(assembly_.payload.data(), assembly_.payload.size()) !=
          assembly_.expected_crc) {
    assembly_.clear();
    queueErrorLocked(connection_generation, id, "invalid_frame",
                     "message length or CRC does not match", true);
    return;
  }
  if (pending_requests_.size() >= kMaxPendingRequests) {
    assembly_.clear();
    return;
  }
  PendingRequest pending;
  pending.id = id;
  pending.connection_generation = connection_generation;
  pending.payload = std::move(assembly_.payload);
  assembly_.clear();
  pending_requests_.push_back(std::move(pending));
}

void BleProtocolService::queueErrorLocked(uint32_t connection_generation,
                                          uint32_t id, const char* code,
                                          const char* message, bool retryable) {
  if (pending_requests_.size() >= kMaxPendingRequests) return;
  PendingRequest pending;
  pending.id = id;
  pending.connection_generation = connection_generation;
  pending.error_code = code;
  pending.error_message = message;
  pending.retryable = retryable;
  pending_requests_.push_back(std::move(pending));
}

void BleProtocolService::refreshAuthenticatedPeer() {
  uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || server_ == nullptr) return;
    connection_handle = connection_handle_;
  }
  const NimBLEConnInfo connection = server_->getPeerInfoByHandle(connection_handle);
  if (!connection.isEncrypted() || !connection.isAuthenticated()) return;
  const String identity = connection.getIdAddress().toString().c_str();
  if (!isKnownBond(identity)) return;
  const bool was_authenticated = authenticated_;
  peer_address_ = identity;
  authenticated_ = true;
  if (!was_authenticated) connection_render_requested_ = true;
}

void BleProtocolService::processPendingRequest() {
  PendingRequest pending;
  {
    const std::lock_guard<std::mutex> lock(rx_mutex_);
    if (pending_requests_.empty()) return;
    pending = std::move(pending_requests_.front());
    pending_requests_.pop_front();
  }
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || pending.connection_generation != connection_generation_) return;
  }
  refreshAuthenticatedPeer();
  request_in_progress_ = true;
  request_connection_generation_ = pending.connection_generation;
  if (pending.error_code != nullptr) {
    sendError(pending.id, pending.error_code, pending.error_message,
              pending.retryable);
  } else {
    processRequest(pending.id, pending.payload.data(), pending.payload.size());
  }
  request_in_progress_ = false;
  request_connection_generation_ = 0;
}

void BleProtocolService::sendDocument(uint32_t id, MessageKind kind,
                                      JsonDocument& document) {
  uint16_t mtu = 23;
  uint32_t connection_generation = 0;
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || tx_ == nullptr) return;
    if (kind == MessageKind::kResponse && request_in_progress_ &&
        request_connection_generation_ != connection_generation_) {
      return;
    }
    mtu = mtu_;
    connection_generation = connection_generation_;
  }
  std::string encoded;
  serializeMsgPack(document, encoded);
  if (encoded.empty() || encoded.size() > kMaxMessageBytes) return;
  const uint32_t checksum = crc32(
      reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
  const size_t gatt_limit = std::max<size_t>(20, mtu > 3 ? mtu - 3 : 20);
  std::vector<std::vector<uint8_t>> frames;
  uint16_t sequence = 0;
  size_t payload_offset = 0;
  do {
    const bool start = sequence == 0;
    const size_t metadata = start ? kStartMetadataBytes : 0;
    const size_t capacity = gatt_limit - kFrameHeaderBytes - metadata;
    const size_t chunk = std::min(capacity, encoded.size() - payload_offset);
    const bool end = payload_offset + chunk == encoded.size();
    std::vector<uint8_t> frame(kFrameHeaderBytes + metadata + chunk);
    frame[0] = kFrameMagic;
    frame[1] = static_cast<uint8_t>(kind) | (start ? kFlagStart : 0U) |
               (end ? kFlagEnd : 0U);
    writeU32(frame.data() + 2, id);
    writeU16(frame.data() + 6, sequence);
    size_t write_offset = kFrameHeaderBytes;
    if (start) {
      writeU16(frame.data() + write_offset,
               static_cast<uint16_t>(encoded.size()));
      writeU32(frame.data() + write_offset + 2, checksum);
      write_offset += kStartMetadataBytes;
    }
    if (chunk > 0) {
      memcpy(frame.data() + write_offset, encoded.data() + payload_offset, chunk);
    }
    frames.push_back(std::move(frame));
    payload_offset += chunk;
    ++sequence;
  } while (payload_offset < encoded.size());
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!connected_ || tx_ == nullptr ||
        connection_generation_ != connection_generation) {
      return;
    }
    for (std::vector<uint8_t>& frame : frames) {
      tx_frames_.push_back(std::move(frame));
    }
  }
  pumpTx();
}

void BleProtocolService::pumpTx() {
  const std::lock_guard<std::mutex> lock(tx_mutex_);
  if (!connected_ || tx_ == nullptr || tx_waiting_for_ack_ ||
      tx_frames_.empty()) {
    return;
  }
  if (tx_retry_at_ != 0 && !deadlineReached(tx_retry_at_)) return;
  const std::vector<uint8_t>& frame = tx_frames_.front();
  if (!tx_->indicate(frame.data(), frame.size(), connection_handle_)) {
    ++tx_send_attempts_;
    if (tx_send_attempts_ >= kTxMaxSendAttempts) {
      TOOLKIT_LOG("ble.tx", "indication could not be queued; dropping TX batch");
      tx_frames_.clear();
      tx_send_attempts_ = 0;
      tx_retry_at_ = 0;
    } else {
      tx_retry_at_ = millis() + kTxRetryDelayMs;
    }
    return;
  }
  tx_frames_.pop_front();
  tx_waiting_for_ack_ = true;
  tx_send_attempts_ = 0;
  tx_retry_at_ = 0;
}

void BleProtocolService::sendError(uint32_t id, const char* code,
                                   const String& message, bool retryable) {
  TOOLKIT_LOG("ble.rpc", String("error id=") + id + " code=" + code +
                             " retryable=" + (retryable ? "yes" : "no"));
  JsonDocument response;
  response["ok"] = false;
  JsonObject error = response["error"].to<JsonObject>();
  error["code"] = code;
  error["message"] = message;
  error["retryable"] = retryable;
  sendDocument(id, MessageKind::kResponse, response);
}

void BleProtocolService::sendEvent(const char* name, JsonVariantConst data) {
  TOOLKIT_LOG("ble.event", String("emit ") + name);
  JsonDocument event;
  event["name"] = name;
  if (!data.isNull()) event["data"].set(data);
  sendDocument(next_event_id_++, MessageKind::kEvent, event);
}

void BleProtocolService::sendSimpleEvent(const char* name) {
  JsonDocument data;
  sendEvent(name, data.as<JsonVariantConst>());
}

bool BleProtocolService::requireTrusted(uint32_t id) {
  if (authenticated_ && isKnownBond(peer_address_)) return true;
  sendError(id, "unauthorized", "a trusted bond is required");
  return false;
}

bool BleProtocolService::requireOwner(uint32_t id) {
  if (isOwner()) return true;
  sendError(id, "forbidden", "the owner bond is required");
  return false;
}

void BleProtocolService::processRequest(uint32_t id, const uint8_t* payload,
                                        size_t length) {
  JsonDocument request;
  const DeserializationError parsed = deserializeMsgPack(request, payload, length);
  if (parsed || !request["op"].is<const char*>() ||
      !request["args"].is<JsonObjectConst>()) {
    sendError(id, "invalid_args", "request must contain op and args maps");
    return;
  }
  const String op = request["op"].as<const char*>();
  JsonObjectConst args = request["args"].as<JsonObjectConst>();
  TOOLKIT_LOG("ble.rpc", String("request id=") + id + " op=" + op +
                             " bytes=" + length);

  if (op == "system.hello") {
    JsonDocument response;
    response["ok"] = true;
    JsonObject result = response["result"].to<JsonObject>();
    result["protocol_major"] = kProtocolMajor;
    result["protocol_minor"] = kProtocolMinor;
    result["firmware"] = EPD_TOOLKIT_VERSION;
    result["device_name"] = deviceName();
    result["max_message_bytes"] = kMaxMessageBytes;
    result["mtu"] = mtu_;
    result["role"] = isOwner() ? "owner" : "trusted";
    result["power_profile"] =
        current_.power.profile == PowerProfile::kBattery ? "battery" : "mains";
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (!requireTrusted(id)) return;

  if (op == "system.status" || op == "diagnostics.get") {
    JsonDocument response;
    response["ok"] = true;
    JsonObject result = response["result"].to<JsonObject>();
    result["uptime_ms"] = millis();
    result["connected"] = connected_.load();
    result["authenticated"] = authenticated_.load();
    result["owner"] = isOwner();
    result["config_revision"] = current_.revision;
    result["resource_count"] = resources_.size();
    result["page_id"] = current_.page.id;
    result["mtu"] = mtu_;
    result["free_heap"] = ESP.getFreeHeap();
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "system.time.set") {
    if (!args["unix_seconds"].is<uint64_t>() ||
        !args["utc_offset_minutes"].is<int16_t>()) {
      sendError(id, "invalid_args", "unix_seconds and utc_offset_minutes are required");
      return;
    }
    const time_t seconds = static_cast<time_t>(args["unix_seconds"].as<uint64_t>());
    timeval value{seconds, 0};
    settimeofday(&value, nullptr);
    utc_offset_minutes_ = args["utc_offset_minutes"].as<int16_t>();
    JsonDocument response;
    response["ok"] = true;
    response["result"]["applied"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "system.sync.complete") {
    const bool scheduled = current_.power.profile == PowerProfile::kBattery;
    JsonDocument response;
    response["ok"] = true;
    response["result"]["sleep_scheduled"] = scheduled;
    response["result"]["wake_in_sec"] = current_.power.wake_interval_sec;
    sendDocument(id, MessageKind::kResponse, response);
    if (scheduled) {
      const std::lock_guard<std::mutex> lock(tx_mutex_);
      sleep_requested_ = true;
    }
    return;
  }
  if (op == "system.restart") {
    if (!requireOwner(id)) return;
    restart_requested_ = true;
    JsonDocument response;
    response["ok"] = true;
    response["result"]["scheduled"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "capabilities.get") {
    JsonDocument response;
    response["ok"] = true;
    JsonObject result = response["result"].to<JsonObject>();
    JsonArray pages = result["pages"].to<JsonArray>();
    for (size_t index = 0; index < pages_.size(); ++index) {
      const PageManifest& manifest = pages_.at(index).manifest();
      JsonObject item = pages.add<JsonObject>();
      item["id"] = manifest.id;
      item["title"] = manifest.title;
      JsonArray slots = item["slots"].to<JsonArray>();
      for (size_t slot_index = 0; slot_index < manifest.slot_count;
           ++slot_index) {
        const PageSlot& slot = manifest.slots[slot_index];
        JsonObject slot_json = slots.add<JsonObject>();
        slot_json["id"] = slot.id;
        if (slot.title != nullptr) slot_json["title"] = slot.title;
        slot_json["status"] =
            slot.status == SlotStatus::kActive ? "active" : "reserved";
        slot_json["required"] = slot.required;
        if (slot.status == SlotStatus::kActive) {
          JsonArray widgets = slot_json["widgets"].to<JsonArray>();
          for (size_t widget_index = 0; widget_index < slot.widget_count;
               ++widget_index) {
            const PageWidget& widget = slot.widgets[widget_index];
            JsonObject widget_json = widgets.add<JsonObject>();
            widget_json["id"] = widget.id;
            widget_json["title"] = widget.title;
            widget_json["schema_id"] = widget.schema_id;
            widget_json["schema_version"] = widget.schema_version;
          }
        }
      }
      JsonArray timed_regions = item["timed_regions"].to<JsonArray>();
      for (size_t region_index = 0;
           region_index < manifest.timed_region_count; ++region_index) {
        const TimedRegion& region = manifest.timed_regions[region_index];
        JsonObject region_json = timed_regions.add<JsonObject>();
        region_json["id"] = region.id;
        region_json["interval_sec"] = region.interval_sec;
        JsonObject bounds = region_json["bounds"].to<JsonObject>();
        bounds["x"] = region.bounds.x;
        bounds["y"] = region.bounds.y;
        bounds["width"] = region.bounds.width;
        bounds["height"] = region.bounds.height;
      }
    }
    result["battery"] = current_.hardware.battery.enabled;
    result["io12"] = current_.hardware.io12_mode == Io12Mode::kKey;
    result["max_resources"] = 16;
    result["max_resource_payload_bytes"] = 2048;
    result["max_page_bindings"] = kMaxPageBindings;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "config.get") {
    JsonDocument response;
    response["ok"] = true;
    configToJson(current_, response["result"]["config"].to<JsonObject>());
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "config.patch") {
    if (!requireOwner(id)) return;
    DeviceConfig candidate = staged_;
    String error;
    if (!applyConfigPatch(args["patch"], candidate, error)) {
      sendError(id, "invalid_args", error);
      return;
    }
    if (!validatePageSettings(candidate.page, pages_, resources_, error)) {
      sendError(id, "invalid_args", error);
      return;
    }
    staged_ = candidate;
    JsonDocument response;
    response["ok"] = true;
    response["result"]["staged"] = true;
    response["result"]["restart_required"] =
        configRequiresRestart(current_, staged_);
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "config.discard") {
    staged_ = current_;
    JsonDocument response;
    response["ok"] = true;
    response["result"]["discarded"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "config.commit") {
    if (!requireOwner(id)) return;
    const uint32_t expected = args["expected_revision"] | current_.revision;
    if (expected != current_.revision) {
      sendError(id, "conflict", "config revision changed");
      return;
    }
    const bool restart = configRequiresRestart(current_, staged_);
    String error;
    if (!validatePageSettings(staged_.page, pages_, resources_, error)) {
      sendError(id, "invalid_args", error);
      return;
    }
    staged_.revision = current_.revision + 1U;
    if (!config_store_.save(staged_, error)) {
      sendError(id, "storage_error", error, true);
      return;
    }
    current_ = staged_;
    render_requested_ = true;
    TOOLKIT_LOG("config", String("committed revision=") + current_.revision +
                              " restart_required=" + (restart ? "yes" : "no"));
    JsonDocument response;
    response["ok"] = true;
    response["result"]["revision"] = current_.revision;
    response["result"]["restart_required"] = restart;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "resource.list") {
    JsonDocument response;
    response["ok"] = true;
    JsonArray items = response["result"]["resources"].to<JsonArray>();
    for (size_t index = 0; index < resources_.size(); ++index) {
      ResourceStore::toJson(resources_.at(index), items.add<JsonObject>(), false);
    }
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "resource.get") {
    const String key = args["key"] | "";
    const ResourceRecord* resource = resources_.get(key);
    if (resource == nullptr) {
      sendError(id, "not_found", "resource not found");
      return;
    }
    JsonDocument response;
    response["ok"] = true;
    ResourceStore::toJson(*resource,
                          response["result"]["resource"].to<JsonObject>(), true);
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "resource.put") {
    String error;
    const ResourcePutResult result = resources_.put(args["resource"], error);
    if (result == ResourcePutResult::kConflict) {
      sendError(id, "conflict", error);
      return;
    }
    if (result == ResourcePutResult::kInvalid) {
      sendError(id, "invalid_args", error);
      return;
    }
    if (result == ResourcePutResult::kFull) {
      sendError(id, "storage_error", error);
      return;
    }
    const bool changed = result != ResourcePutResult::kUnchanged;
    bool render_scheduled = false;
    if (changed) {
      String save_error;
      resources_.saveIfDue(static_cast<uint64_t>(time(nullptr)), false,
                           save_error);
      const String changed_key = args["resource"]["key"] | "";
      IPage* page = pages_.find(current_.page.id);
      if (page != nullptr &&
          PageResources(page->manifest(), current_.page, resources_, unixNow())
              .usesKey(changed_key)) {
        render_requested_ = true;
        render_scheduled = true;
      }
    }
    TOOLKIT_LOG("resource", String("put key=") +
                                (args["resource"]["key"] | "") +
                                " changed=" + (changed ? "yes" : "no"));
    JsonDocument response;
    response["ok"] = true;
    response["result"]["changed"] = changed;
    response["result"]["render_scheduled"] = render_scheduled;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "resource.delete") {
    if (!requireOwner(id)) return;
    const String key = args["key"] | "";
    IPage* page = pages_.find(current_.page.id);
    const bool active_binding =
        page != nullptr &&
        PageResources(page->manifest(), current_.page, resources_, unixNow())
            .usesKey(key);
    String error;
    if (!resources_.remove(key, error)) {
      sendError(id, "not_found", error);
      return;
    }
    resources_.saveIfDue(static_cast<uint64_t>(time(nullptr)), true, error);
    if (active_binding) render_requested_ = true;
    TOOLKIT_LOG("resource", String("deleted key=") + key);
    JsonDocument response;
    response["ok"] = true;
    response["result"]["deleted"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "page.get") {
    JsonDocument response;
    response["ok"] = true;
    JsonObject page = response["result"]["page"].to<JsonObject>();
    page["id"] = current_.page.id;
    JsonObject bindings = page["bindings"].to<JsonObject>();
    for (size_t index = 0; index < current_.page.binding_count; ++index) {
      const PageBinding& source = current_.page.bindings[index];
      JsonObject binding = bindings[source.slot_id].to<JsonObject>();
      if (!source.widget_id.isEmpty()) binding["widget_id"] = source.widget_id;
      binding["resource_key"] = source.resource_key;
    }
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "page.set") {
    if (!requireOwner(id)) return;
    if (!args["page"].is<JsonObjectConst>()) {
      sendError(id, "invalid_args", "page must be an object");
      return;
    }
    JsonObjectConst page_json = args["page"].as<JsonObjectConst>();
    if (!page_json["id"].is<const char*>() ||
        !page_json["bindings"].is<JsonObjectConst>()) {
      sendError(id, "invalid_args", "page.id and page.bindings are required");
      return;
    }
    PageSettings settings;
    settings.id = page_json["id"].as<const char*>();
    settings.binding_count = 0;
    for (JsonPairConst pair : page_json["bindings"].as<JsonObjectConst>()) {
      if (settings.binding_count >= kMaxPageBindings) {
        sendError(id, "invalid_args", "page bindings are invalid or exceed 8");
        return;
      }
      PageBinding& binding = settings.bindings[settings.binding_count++];
      binding.slot_id = pair.key().c_str();
      binding.widget_id = "";
      if (pair.value().is<const char*>()) {
        binding.resource_key = pair.value().as<const char*>();
      } else if (pair.value().is<JsonObjectConst>()) {
        JsonObjectConst value = pair.value().as<JsonObjectConst>();
        if (!value["resource_key"].is<const char*>() ||
            (!value["widget_id"].isNull() &&
             !value["widget_id"].is<const char*>())) {
          sendError(id, "invalid_args", "page binding fields are invalid");
          return;
        }
        binding.widget_id = value["widget_id"] | "";
        binding.resource_key = value["resource_key"].as<const char*>();
      } else {
        sendError(id, "invalid_args", "page binding must be an object");
        return;
      }
    }
    DeviceConfig candidate = current_;
    candidate.page = settings;
    String error;
    if (!validateConfig(candidate, error) ||
        !validatePageSettings(settings, pages_, resources_, error)) {
      sendError(id, "invalid_args", error);
      return;
    }
    candidate.revision += 1U;
    if (!config_store_.save(candidate, error)) {
      sendError(id, "storage_error", error, true);
      return;
    }
    current_ = candidate;
    staged_ = current_;
    render_requested_ = true;
    IPage* page = pages_.find(current_.page.id);
    page_freshness_signature_ =
        page == nullptr
            ? 0
            : PageResources(page->manifest(), current_.page, resources_, unixNow())
                  .freshnessSignature();
    TOOLKIT_LOG("page", String("selected page=") + current_.page.id);
    JsonDocument response;
    response["ok"] = true;
    response["result"]["revision"] = current_.revision;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "display.refresh") {
    const String refresh_mode = args["mode"] | "auto";
    force_full_requested_ = refresh_mode == "full";
    render_requested_ = true;
    TOOLKIT_LOG("display", String("refresh scheduled mode=") + refresh_mode);
    JsonDocument response;
    response["ok"] = true;
    response["result"]["scheduled"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "security.owner.get") {
    JsonDocument response;
    response["ok"] = true;
    response["result"]["role"] = isOwner() ? "owner" : "trusted";
    response["result"]["owned"] = !owner_address_.isEmpty();
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "security.enrollment.open") {
    if (!requireOwner(id)) return;
    String error;
    if (!enterSetupMode(error)) {
      sendError(id, "conflict", error);
      return;
    }
    JsonDocument response;
    response["ok"] = true;
    response["result"]["expires_in_sec"] = 120;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "security.enrollment.close") {
    if (!requireOwner(id)) return;
    closeSetupMode();
    TOOLKIT_LOG("security", "enrollment closed");
    JsonDocument response;
    response["ok"] = true;
    response["result"]["closed"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "security.bonds.list") {
    JsonDocument response;
    response["ok"] = true;
    JsonArray bonds = response["result"]["bonds"].to<JsonArray>();
    for (int index = 0; index < NimBLEDevice::getNumBonds(); ++index) {
      const NimBLEAddress address = NimBLEDevice::getBondedAddress(index);
      JsonObject bond = bonds.add<JsonObject>();
      bond["id"] = bondId(address);
      bond["role"] = owner_address_ == address.toString().c_str()
                           ? "owner"
                           : "trusted";
    }
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "security.bonds.revoke" || op == "security.owner.transfer") {
    if (!requireOwner(id)) return;
    NimBLEAddress address;
    if (!findBond(args["bond_id"] | "", address)) {
      sendError(id, "not_found", "bond not found");
      return;
    }
    const String address_text = address.toString().c_str();
    if (op == "security.owner.transfer") {
      saveOwner(address);
      TOOLKIT_LOG("security", "ownership transferred");
    } else {
      if (address_text == owner_address_) {
        sendError(id, "conflict", "transfer ownership before revoking owner");
        return;
      }
      NimBLEDevice::deleteBond(address);
      known_bonds_.erase(
          std::remove(known_bonds_.begin(), known_bonds_.end(), address_text),
          known_bonds_.end());
      TOOLKIT_LOG("security", "trusted bond revoked");
    }
    JsonDocument response;
    response["ok"] = true;
    response["result"]["applied"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "factory_reset.prepare") {
    if (!requireOwner(id)) return;
    factory_code_ = 100000U + esp_random() % 900000U;
    factory_deadline_ = millis() + 30000U;
    factory_code_pending_ = true;
    TOOLKIT_LOG("security", "factory reset confirmation prepared");
    JsonDocument response;
    response["ok"] = true;
    response["result"]["expires_in_sec"] = 30;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  if (op == "factory_reset.commit") {
    if (!requireOwner(id)) return;
    if ((args["code"] | 0U) != factory_code_ ||
        deadlineReached(factory_deadline_)) {
      sendError(id, "unauthorized", "confirmation code is invalid or expired");
      return;
    }
    String error;
    if (!factoryReset(error)) {
      sendError(id, "storage_error", error, true);
      return;
    }
    JsonDocument response;
    response["ok"] = true;
    response["result"]["scheduled"] = true;
    sendDocument(id, MessageKind::kResponse, response);
    return;
  }
  sendError(id, "not_found", "operation is not supported");
}

bool BleProtocolService::factoryReset(String& error) {
  if (!config_store_.erase(error) || !resources_.erase(error)) return false;
  clearSecurityState();
  NimBLEDevice::deleteAllBonds();
  factory_reset_requested_ = true;
  TOOLKIT_LOG("security", "factory state cleared; restart scheduled");
  return true;
}

bool BleProtocolService::setupMode() const {
  return enrollment_deadline_ != 0 &&
         !deadlineReached(enrollment_deadline_);
}

uint32_t BleProtocolService::setupRemainingSeconds() const {
  if (!setupMode()) return 0;
  const uint32_t remaining = enrollment_deadline_ - millis();
  return (remaining + 999U) / 1000U;
}

bool BleProtocolService::enterSetupMode(String& error) {
  error = "";
  if (!active_) {
    error = "BLE service is not active";
    return false;
  }
  if (NimBLEDevice::getNumBonds() >= 4) {
    error = "bond limit has been reached";
    return false;
  }

  enrollment_deadline_ = millis() + kEnrollmentWindowMs;
  fast_advertising_deadline_ = enrollment_deadline_;
  setup_render_requested_ = true;
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    battery_awake_deadline_ = enrollment_deadline_;
    sleep_requested_ = false;
  }

  char formatted_passkey[7];
  snprintf(formatted_passkey, sizeof(formatted_passkey), "%06lu",
           static_cast<unsigned long>(passkey_));
  TOOLKIT_LOG("security", String("setup opened for 120s passkey=") +
                              formatted_passkey);

  if (!connected_) {
    NimBLEDevice::stopAdvertising();
    if (!configureAdvertising(true) || !NimBLEDevice::startAdvertising()) {
      const std::lock_guard<std::mutex> lock(tx_mutex_);
      advertising_restart_at_ = millis() + kAdvertisingRetryDelayMs;
      TOOLKIT_LOG("ble", "setup advertising refresh failed; retry scheduled");
    } else {
      TOOLKIT_LOG("ble", "setup fast advertising active");
    }
  } else if (!authenticated_ && server_) {
    server_->disconnect(connection_handle_);
  }
  return true;
}

void BleProtocolService::closeSetupMode() {
  enrollment_deadline_ = 0;
  if (current_.power.profile == PowerProfile::kBattery) {
    fast_advertising_deadline_ = 0;
  }
  setup_render_requested_ = false;
  if (!owner_address_.isEmpty()) connection_render_requested_ = true;
  if (!active_ || connected_) return;

  const bool fast = current_.power.profile == PowerProfile::kMains ||
                    (fast_advertising_deadline_ != 0 &&
                     !deadlineReached(fast_advertising_deadline_));
  NimBLEDevice::stopAdvertising();
  if (!configureAdvertising(fast) || !NimBLEDevice::startAdvertising()) {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    advertising_restart_at_ = millis() + kAdvertisingRetryDelayMs;
    TOOLKIT_LOG("ble", "advertising refresh failed; retry scheduled");
  }
}

void BleProtocolService::loop() {
  if (!active_) return;
  pumpTx();
  processPendingRequest();
  bool disconnected = false;
  bool restart_advertising = false;
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    disconnected = !connected_;
    if (disconnected && deadlineReached(advertising_restart_at_)) {
      restart_advertising = true;
      advertising_restart_at_ = 0;
    }
  }
  if (restart_advertising) {
    if (!configureAdvertising(true) || !NimBLEDevice::startAdvertising()) {
      const std::lock_guard<std::mutex> lock(tx_mutex_);
      advertising_restart_at_ = millis() + kAdvertisingRetryDelayMs;
      TOOLKIT_LOG("ble", "advertising restart failed; retry scheduled");
    } else {
      TOOLKIT_LOG("ble", "fast advertising restarted");
    }
  }
  if (disconnected && current_.power.profile == PowerProfile::kBattery &&
      deadlineReached(fast_advertising_deadline_)) {
    NimBLEDevice::stopAdvertising();
    if (!configureAdvertising(false) || !NimBLEDevice::startAdvertising()) {
      const std::lock_guard<std::mutex> lock(tx_mutex_);
      advertising_restart_at_ = millis() + kAdvertisingRetryDelayMs;
      fast_advertising_deadline_ = millis() + 30000U;
      TOOLKIT_LOG("ble", "low-duty advertising failed; retry scheduled");
    } else {
      fast_advertising_deadline_ = 0;
      TOOLKIT_LOG("ble", "low-duty advertising active");
    }
  }
  if (deadlineReached(enrollment_deadline_)) {
    closeSetupMode();
    TOOLKIT_LOG("security", "setup window expired");
  }
  if (setup_render_requested_.exchange(false)) {
    display_.renderPairing(passkey_, !owner_address_.isEmpty(), sessionReady());
    display_.present(current_.display);
  }
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    if (current_.power.profile == PowerProfile::kBattery &&
        !owner_address_.isEmpty() && deadlineReached(battery_awake_deadline_)) {
      sleep_requested_ = true;
    }
  }
  const uint64_t now = unixNow();
  IPage* page = pages_.find(current_.page.id);
  const uint32_t signature =
      page == nullptr
          ? 0
          : PageResources(page->manifest(), current_.page, resources_, now)
                .freshnessSignature();
  if (signature != page_freshness_signature_) {
    page_freshness_signature_ = signature;
    render_requested_ = true;
    TOOLKIT_LOG("resource", "active page resource state changed");
  }
  {
    const std::lock_guard<std::mutex> lock(rx_mutex_);
    if (assembly_.active && millis() - assembly_.started_at > kAssemblyTimeoutMs) {
      assembly_.clear();
      TOOLKIT_LOG("ble.rpc", "message assembly expired");
    }
  }
}

void BleProtocolService::stop() {
  if (!active_ && server_ == nullptr) return;
  bool was_connected = false;
  uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
  {
    const std::lock_guard<std::mutex> lock(tx_mutex_);
    was_connected = connected_;
    connection_handle = connection_handle_;
    connected_ = false;
    connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
    advertising_restart_at_ = 0;
    mtu_ = 23;
    ++connection_generation_;
    tx_frames_.clear();
    tx_waiting_for_ack_ = false;
    tx_send_attempts_ = 0;
    tx_retry_at_ = 0;
  }
  active_ = false;
  NimBLEDevice::stopAdvertising();
  if (server_) server_->setCallbacks(nullptr, false);
  if (rx_) rx_->setCallbacks(nullptr);
  if (tx_) tx_->setCallbacks(nullptr);
  if (server_ && was_connected) server_->disconnect(connection_handle);
  NimBLEDevice::deinit(true);
  delete characteristic_callbacks_;
  delete server_callbacks_;
  characteristic_callbacks_ = nullptr;
  server_callbacks_ = nullptr;
  server_ = nullptr;
  rx_ = nullptr;
  tx_ = nullptr;
  battery_level_ = nullptr;
  authenticated_ = false;
  connection_render_requested_ = false;
  setup_render_requested_ = false;
  enrollment_deadline_ = 0;
  {
    const std::lock_guard<std::mutex> lock(rx_mutex_);
    assembly_.clear();
    pending_requests_.clear();
  }
  TOOLKIT_LOG("ble", "BLE service stopped");
}

bool BleProtocolService::takeRenderRequest(bool& force_full) {
  const bool connection_changed = connection_render_requested_.exchange(false);
  if (setupMode()) return false;
  if (!render_requested_ && !connection_changed) return false;
  render_requested_ = false;
  force_full = force_full_requested_;
  force_full_requested_ = false;
  return true;
}

bool BleProtocolService::takeRestartRequest() {
  const bool value = restart_requested_;
  restart_requested_ = false;
  return value;
}

bool BleProtocolService::takeFactoryResetRequest() {
  const bool value = factory_reset_requested_;
  factory_reset_requested_ = false;
  return value;
}

bool BleProtocolService::takeFactoryCode(uint32_t& code) {
  if (!factory_code_pending_) return false;
  factory_code_pending_ = false;
  code = factory_code_;
  return true;
}

bool BleProtocolService::takeSleepRequest() {
  const std::lock_guard<std::mutex> lock(tx_mutex_);
  if (!sleep_requested_ || tx_waiting_for_ack_ || !tx_frames_.empty()) {
    return false;
  }
  sleep_requested_ = false;
  return true;
}

void BleProtocolService::emitKeyPressed() { sendSimpleEvent("input.key"); }

void BleProtocolService::updateBattery(uint16_t battery_mv) {
  if (battery_level_ == nullptr) return;
  uint8_t percent = battery_mv <= 3300
                        ? 0
                        : battery_mv >= 4200
                              ? 100
                              : static_cast<uint8_t>((battery_mv - 3300U) / 9U);
  battery_level_->setValue(&percent, sizeof(percent));
  battery_level_->notify();
  TOOLKIT_LOG("power", String("battery characteristic updated ") + battery_mv +
                           "mV " + static_cast<unsigned int>(percent) + "%");
  JsonDocument data;
  data["millivolts"] = battery_mv;
  data["percent"] = percent;
  sendEvent("battery.updated", data.as<JsonVariantConst>());
}

void BleProtocolService::emitDisplayStarted(bool full) {
  JsonDocument data;
  data["mode"] = full ? "full" : "auto";
  sendEvent("display.started", data.as<JsonVariantConst>());
}

void BleProtocolService::emitDisplayCompleted(const char* result) {
  JsonDocument data;
  data["result"] = result;
  sendEvent("display.completed", data.as<JsonVariantConst>());
}

}  // namespace epd
