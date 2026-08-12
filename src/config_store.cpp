#include "toolkit/config_store.h"

#include <memory>
#include <string>
#include <vector>

#include "toolkit/core_logic.h"
#include "toolkit/log.h"

namespace epd {

uint32_t ConfigStore::crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

ConfigStore::SlotData ConfigStore::readSlot(Preferences& preferences,
                                            const char* key) {
  SlotData slot;
  const size_t length = preferences.getBytesLength(key);
  if (length < sizeof(SlotHeader) ||
      length > sizeof(SlotHeader) + kLegacyMaxPayloadBytes) {
    return slot;
  }

  std::vector<uint8_t> bytes(length);
  if (preferences.getBytes(key, bytes.data(), bytes.size()) != bytes.size()) return slot;

  SlotHeader header{};
  memcpy(&header, bytes.data(), sizeof(header));
  if ((header.magic != kSlotMagic && header.magic != kLegacySlotMagic) ||
      header.schema != kConfigSchemaVersion ||
      (header.magic == kSlotMagic && header.payload_length > kMaxPayloadBytes) ||
      header.payload_length != length - sizeof(header)) {
    return slot;
  }
  const uint8_t* payload = bytes.data() + sizeof(header);
  if (crc32(payload, header.payload_length) != header.crc32) return slot;

  slot.payload.assign(payload, payload + header.payload_length);
  slot.msgpack = header.magic == kSlotMagic;
  slot.sequence = header.sequence;
  slot.valid = true;
  return slot;
}

bool ConfigStore::load(DeviceConfig& config) {
  TOOLKIT_LOG("config", "loading NVS snapshot");
  Preferences preferences;
  // Read-write mode creates the namespace on a pristine NVS partition. This
  // avoids Preferences logging NOT_FOUND as an error during normal first boot.
  if (!preferences.begin("epd_cfg4", false)) {
    TOOLKIT_LOG("config", "cannot open NVS namespace");
    return false;
  }
  const SlotData a = readSlot(preferences, "slot_a");
  const SlotData b = readSlot(preferences, "slot_b");
  const uint8_t active = preferences.getUChar("active", 0);
  preferences.end();

  const core::ConfigSlotChoice choice = core::selectConfigSlot(
      a.valid, a.sequence, b.valid, b.sequence, active);
  const SlotData* selected = choice == core::ConfigSlotChoice::kA
                                 ? &a
                                 : choice == core::ConfigSlotChoice::kB ? &b
                                                                        : nullptr;
  if (selected == nullptr) {
    TOOLKIT_LOG("config", "no valid config slot");
    return false;
  }

  JsonDocument document;
  const DeserializationError decoded =
      selected->msgpack
          ? deserializeMsgPack(document, selected->payload.data(),
                               selected->payload.size())
          : deserializeJson(document, selected->payload.data(),
                            selected->payload.size());
  if (decoded != DeserializationError::Ok) {
    TOOLKIT_LOG("config", "config snapshot decode failed");
    return false;
  }
  String error;
  if (!configFromJson(document.as<JsonVariantConst>(), config, error)) {
    TOOLKIT_LOG("config", String("config validation failed: ") + error);
    return false;
  }
  TOOLKIT_LOG("config", String("loaded revision=") + config.revision);
  if (!selected->msgpack) {
    String migration_error;
    if (!save(config, migration_error) || !save(config, migration_error)) {
      TOOLKIT_LOG("config", String("MessagePack migration failed: ") +
                                migration_error);
    } else {
      TOOLKIT_LOG("config", "migrated both config slots to MessagePack");
    }
  }
  return true;
}

bool ConfigStore::save(const DeviceConfig& config, String& error) {
  if (!validateConfig(config, error)) {
    TOOLKIT_LOG("config", String("save validation failed: ") + error);
    return false;
  }

  JsonDocument document;
  configToJson(config, document.to<JsonObject>());
  std::string payload;
  serializeMsgPack(document, payload);
  if (payload.size() > kMaxPayloadBytes) {
    error = "serialized config is too large";
    TOOLKIT_LOG("config", error);
    return false;
  }

  Preferences preferences;
  if (!preferences.begin("epd_cfg4", false)) {
    error = "cannot open NVS namespace";
    TOOLKIT_LOG("config", error);
    return false;
  }
  const SlotData a = readSlot(preferences, "slot_a");
  const SlotData b = readSlot(preferences, "slot_b");
  const uint8_t current_active = preferences.getUChar("active", 0);
  const uint8_t next_active = current_active == 0 ? 1 : 0;
  const char* next_key = next_active == 0 ? "slot_a" : "slot_b";
  const uint32_t sequence = std::max(a.sequence, b.sequence) + 1U;

  SlotHeader header{kSlotMagic, kConfigSchemaVersion, 0, sequence,
                    static_cast<uint32_t>(payload.size()),
                    crc32(reinterpret_cast<const uint8_t*>(payload.data()),
                          payload.size())};
  std::vector<uint8_t> bytes(sizeof(header) + payload.size());
  memcpy(bytes.data(), &header, sizeof(header));
  memcpy(bytes.data() + sizeof(header), payload.data(), payload.size());

  if (preferences.putBytes(next_key, bytes.data(), bytes.size()) != bytes.size()) {
    preferences.end();
    error = "cannot write NVS config slot";
    TOOLKIT_LOG("config", error);
    return false;
  }
  const SlotData verify = readSlot(preferences, next_key);
  if (!verify.valid || verify.sequence != sequence ||
      preferences.putUChar("active", next_active) != 1) {
    preferences.end();
    error = "NVS config verification failed";
    TOOLKIT_LOG("config", error);
    return false;
  }
  preferences.end();
  TOOLKIT_LOG("config", String("saved revision=") + config.revision +
                            " sequence=" + sequence);
  return true;
}

bool ConfigStore::erase(String& error) {
  Preferences preferences;
  if (!preferences.begin("epd_cfg4", false)) {
    error = "cannot open NVS namespace";
    TOOLKIT_LOG("config", error);
    return false;
  }
  const bool ok = preferences.clear();
  preferences.end();
  if (!ok) {
    error = "cannot erase NVS config";
    TOOLKIT_LOG("config", error);
  } else {
    TOOLKIT_LOG("config", "NVS namespace erased");
  }
  return ok;
}

}  // namespace epd
