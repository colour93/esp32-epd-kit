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
    error = "resource payload exceeds 4096 bytes";
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
    *existing = candidate;
    dirty_ = dirty_ || candidate.persistence == ResourcePersistence::kSnapshot;
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
  if (!preferences.begin("epd_res3", false)) {
    error = "cannot open resource namespace";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  const String snapshot = preferences.getString("snapshot", "");
  last_saved_at_ = preferences.getULong64("stored_at", 0);
  preferences.end();
  if (snapshot.isEmpty()) {
    TOOLKIT_LOG("resource", "no stored snapshot");
    return true;
  }
  JsonDocument document;
  if (deserializeJson(document, snapshot) != DeserializationError::Ok ||
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
    records_.push_back(record);
  }
  dirty_ = false;
  TOOLKIT_LOG("resource", String("loaded count=") + records_.size());
  return true;
}

bool ResourceStore::saveIfDue(uint64_t now, bool force, String& error) {
  if (!dirty_) return true;
  if (!force && last_saved_at_ > 0 && now >= last_saved_at_ &&
      now - last_saved_at_ < kMinWriteIntervalSec) {
    return true;
  }
  JsonDocument document;
  JsonArray resources = document["resources"].to<JsonArray>();
  for (const ResourceRecord& record : records_) {
    if (record.persistence != ResourcePersistence::kSnapshot) continue;
    toJson(record, resources.add<JsonObject>(), true);
  }
  String snapshot;
  serializeJson(document, snapshot);
  if (snapshot.length() > kMaxSnapshotBytes) {
    error = "resource snapshot exceeds 16384 bytes";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  Preferences preferences;
  if (!preferences.begin("epd_res3", false)) {
    error = "cannot open resource namespace";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  const bool ok = preferences.putString("snapshot", snapshot) == snapshot.length() &&
                  preferences.putULong64("stored_at", now) == sizeof(uint64_t);
  preferences.end();
  if (!ok) {
    error = "cannot write resource snapshot";
    TOOLKIT_LOG("resource", error);
    return false;
  }
  last_saved_at_ = now;
  dirty_ = false;
  TOOLKIT_LOG("resource", String("snapshot saved count=") + records_.size());
  return true;
}

bool ResourceStore::erase(String& error) {
  records_.clear();
  Preferences preferences;
  if (!preferences.begin("epd_res3", false)) {
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
