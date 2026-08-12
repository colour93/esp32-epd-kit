#include "toolkit/resource_store.h"

#include <Preferences.h>

#include <string>

#include "toolkit/log.h"

namespace epd {
namespace {

bool validText(const String& value, size_t maximum) {
  if (value.isEmpty() || value.length() > maximum) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t byte = static_cast<uint8_t>(value[i]);
    if (byte < 0x20U || byte == 0x7FU) return false;
  }
  return true;
}

}  // namespace

uint32_t ResourceStore::crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

uint32_t ResourceStore::contentCrc(const ResourceRecord& record) {
  JsonDocument document;
  JsonObject root = document.to<JsonObject>();
  toJson(record, root, true);
  root.remove("content_crc");
  std::string encoded;
  serializeMsgPack(document, encoded);
  return crc32(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
}

bool ResourceStore::parseRecord(JsonVariantConst value, ResourceRecord& out,
                                String& error) {
  if (!value.is<JsonObjectConst>()) {
    error = "resource must be an object";
    return false;
  }
  if (!value["key"].is<const char*>() ||
      !value["schema_id"].is<const char*>() ||
      !value["schema_version"].is<uint16_t>() ||
      !value["revision"].is<uint64_t>() ||
      !value["updated_at"].is<uint64_t>() ||
      !value["ttl_sec"].is<uint32_t>() ||
      !value["payload"].is<JsonObjectConst>()) {
    error = "resource metadata or payload has an invalid type";
    return false;
  }
  out.key = value["key"].as<const char*>();
  out.schema_id = value["schema_id"].as<const char*>();
  out.schema_version = value["schema_version"].as<uint16_t>();
  out.revision = value["revision"].as<uint64_t>();
  out.updated_at = value["updated_at"].as<uint64_t>();
  out.ttl_sec = value["ttl_sec"].as<uint32_t>();
  const String persistence = value["persistence"] | "volatile";
  if (persistence == "volatile") {
    out.persistence = ResourcePersistence::kVolatile;
  } else if (persistence == "snapshot") {
    out.persistence = ResourcePersistence::kSnapshot;
  } else {
    error = "persistence must be volatile or snapshot";
    return false;
  }
  if (!validText(out.key, 64) || !validText(out.schema_id, 64) ||
      out.schema_version == 0 || out.revision == 0 || out.ttl_sec > 604800U) {
    error = "resource metadata is outside supported limits";
    return false;
  }
  std::string payload;
  serializeMsgPack(value["payload"], payload);
  if (payload.size() > kMaxPayloadBytes) {
    error = "resource payload exceeds 2048 bytes";
    return false;
  }
  out.payload.set(value["payload"]);
  out.content_crc = contentCrc(out);
  return true;
}

ResourcePutResult ResourceStore::put(JsonVariantConst value, String& error) {
  ResourceRecord candidate;
  if (!parseRecord(value, candidate, error)) {
    TOOLKIT_LOG("resource", String("put rejected: ") + error);
    return ResourcePutResult::kInvalid;
  }
  ResourceRecord* existing = get(candidate.key);
  if (existing != nullptr) {
    if (candidate.revision < existing->revision) {
      error = "resource revision is older than the stored revision";
      TOOLKIT_LOG("resource", String("revision conflict key=") + candidate.key);
      return ResourcePutResult::kConflict;
    }
    if (candidate.revision == existing->revision) {
      if (candidate.content_crc == existing->content_crc) {
        TOOLKIT_LOG("resource", String("idempotent put key=") + candidate.key +
                                    " revision=" + candidate.revision);
        return ResourcePutResult::kUnchanged;
      }
      error = "resource revision was reused with different content";
      return ResourcePutResult::kConflict;
    }
    dirty_ = dirty_ ||
             existing->persistence == ResourcePersistence::kSnapshot ||
             candidate.persistence == ResourcePersistence::kSnapshot;
    *existing = candidate;
    TOOLKIT_LOG("resource", String("updated key=") + candidate.key +
                                " revision=" + candidate.revision);
    return ResourcePutResult::kUpdated;
  }
  if (records_.size() >= kMaxResources) {
    error = "resource store is full";
    TOOLKIT_LOG("resource", error);
    return ResourcePutResult::kFull;
  }
  records_.push_back(candidate);
  dirty_ = dirty_ || candidate.persistence == ResourcePersistence::kSnapshot;
  TOOLKIT_LOG("resource", String("created key=") + candidate.key +
                              " revision=" + candidate.revision);
  return ResourcePutResult::kCreated;
}

const ResourceRecord* ResourceStore::get(const String& key) const {
  for (const ResourceRecord& record : records_) {
    if (record.key == key) return &record;
  }
  return nullptr;
}

ResourceRecord* ResourceStore::get(const String& key) {
  for (ResourceRecord& record : records_) {
    if (record.key == key) return &record;
  }
  return nullptr;
}

bool ResourceStore::remove(const String& key, String& error) {
  for (auto it = records_.begin(); it != records_.end(); ++it) {
    if (it->key != key) continue;
    dirty_ = dirty_ || it->persistence == ResourcePersistence::kSnapshot;
    TOOLKIT_LOG("resource", String("removed key=") + key);
    records_.erase(it);
    return true;
  }
  error = "resource not found";
  return false;
}

void ResourceStore::toJson(const ResourceRecord& record, JsonObject out,
                           bool include_payload) {
  out["key"] = record.key;
  out["schema_id"] = record.schema_id;
  out["schema_version"] = record.schema_version;
  out["revision"] = record.revision;
  out["updated_at"] = record.updated_at;
  out["ttl_sec"] = record.ttl_sec;
  out["persistence"] = record.persistence == ResourcePersistence::kSnapshot
                           ? "snapshot"
                           : "volatile";
  out["content_crc"] = record.content_crc;
  if (include_payload) out["payload"].set(record.payload.as<JsonVariantConst>());
}

bool ResourceStore::load(String& error) {
  records_.clear();
  TOOLKIT_LOG("resource", "loading NVS snapshot");
  Preferences preferences;
  if (!preferences.begin("epd_res4", false)) {
    error = "cannot open resource namespace";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  std::vector<uint8_t> snapshot;
  bool legacy_json = false;
  if (preferences.getType("snapshot5") == PT_BLOB) {
    const size_t length = preferences.getBytesLength("snapshot5");
    if (length > 0 && length <= kMaxSnapshotBytes) {
      snapshot.resize(length);
      if (preferences.getBytes("snapshot5", snapshot.data(), snapshot.size()) !=
          snapshot.size()) {
        snapshot.clear();
      }
    }
  } else if (preferences.getType("snapshot") == PT_STR) {
    const String encoded = preferences.getString("snapshot", "");
    snapshot.assign(reinterpret_cast<const uint8_t*>(encoded.c_str()),
                    reinterpret_cast<const uint8_t*>(encoded.c_str()) +
                        encoded.length());
    legacy_json = !snapshot.empty();
  }
  last_saved_at_ = preferences.getULong64("stored_at", 0);
  preferences.end();
  if (snapshot.empty()) {
    TOOLKIT_LOG("resource", "no stored snapshot");
    return true;
  }
  JsonDocument document;
  const DeserializationError decoded =
      legacy_json ? deserializeJson(document, snapshot.data(), snapshot.size())
                  : deserializeMsgPack(document, snapshot.data(), snapshot.size());
  if (decoded != DeserializationError::Ok ||
      !document["resources"].is<JsonArrayConst>()) {
    error = "resource snapshot is invalid";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  for (JsonVariantConst value : document["resources"].as<JsonArrayConst>()) {
    if (records_.size() >= kMaxResources) break;
    ResourceRecord record;
    String parse_error;
    if (!parseRecord(value, record, parse_error)) continue;
    record.persistence = ResourcePersistence::kSnapshot;
    record.content_crc = contentCrc(record);
    records_.push_back(record);
  }
  dirty_ = legacy_json;
  if (legacy_json) last_saved_at_ = 0;
  TOOLKIT_LOG("resource", String("loaded count=") + records_.size());
  return true;
}

bool ResourceStore::saveIfDue(uint64_t now, bool force, String& error) {
  if (!dirty_) return true;
  const uint32_t attempt_ms = millis();
  if (!force && last_save_attempt_ms_ != 0 &&
      attempt_ms - last_save_attempt_ms_ < kFailedWriteRetryMs) {
    return true;
  }
  if (!force && last_saved_at_ > 0 && now >= last_saved_at_ &&
      now - last_saved_at_ < kMinWriteIntervalSec) {
    return true;
  }
  last_save_attempt_ms_ = attempt_ms;
  JsonDocument document;
  JsonArray resources = document["resources"].to<JsonArray>();
  for (const ResourceRecord& record : records_) {
    if (record.persistence != ResourcePersistence::kSnapshot) continue;
    JsonObject stored = resources.add<JsonObject>();
    toJson(record, stored, true);
    stored.remove("persistence");
    stored.remove("content_crc");
  }
  std::string snapshot;
  serializeMsgPack(document, snapshot);
  if (snapshot.size() > kMaxSnapshotBytes) {
    error = "resource snapshot exceeds 4096 bytes";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  Preferences preferences;
  if (!preferences.begin("epd_res4", false)) {
    error = "cannot open resource namespace";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  const bool ok =
      preferences.putBytes("snapshot5", snapshot.data(), snapshot.size()) ==
          snapshot.size() &&
      preferences.putULong64("stored_at", now) == sizeof(uint64_t);
  if (ok && preferences.getType("snapshot") == PT_STR) {
    preferences.remove("snapshot");
  }
  preferences.end();
  if (!ok) {
    error = "cannot write resource snapshot";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  last_saved_at_ = now;
  last_save_attempt_ms_ = 0;
  dirty_ = false;
  TOOLKIT_LOG("resource", String("MessagePack snapshot saved count=") +
                              records_.size() + " bytes=" + snapshot.size());
  return true;
}

bool ResourceStore::erase(String& error) {
  records_.clear();
  Preferences preferences;
  if (!preferences.begin("epd_res4", false)) {
    error = "cannot open resource namespace";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  const bool ok = preferences.clear();
  preferences.end();
  dirty_ = false;
  if (!ok) {
    error = "cannot erase resource namespace";
    TOOLKIT_LOG("resource", error);
  } else {
    TOOLKIT_LOG("resource", "NVS namespace erased");
  }
  return ok;
}

}  // namespace epd
