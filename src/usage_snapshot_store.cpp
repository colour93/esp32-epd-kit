#include "toolkit/usage_snapshot_store.h"

#include <ArduinoJson.h>

#include <cstring>
#include <vector>

namespace epd {
namespace {

void windowToJson(const RateLimitWindow& window, JsonObject out) {
  out["present"] = window.present;
  out["used_percent"] = window.used_percent;
  out["limit_window_seconds"] = window.limit_window_seconds;
  out["reset_after_seconds"] = window.reset_after_seconds;
  out["reset_at"] = window.reset_at;
}

RateLimitWindow windowFromJson(JsonVariantConst value) {
  RateLimitWindow window;
  if (!value.is<JsonObjectConst>()) return window;
  window.present = value["present"] | false;
  window.used_percent = value["used_percent"] | 0.0F;
  window.limit_window_seconds = value["limit_window_seconds"] | 0U;
  window.reset_after_seconds = value["reset_after_seconds"] | 0U;
  window.reset_at = value["reset_at"] | 0ULL;
  window.kind = core::identifyWindow(window.limit_window_seconds);
  return window;
}

}  // namespace

uint32_t UsageSnapshotStore::crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

UsageSnapshotStore::Record UsageSnapshotStore::readRecord(
    Preferences& preferences) {
  Record record;
  const size_t length = preferences.getBytesLength("snapshot");
  if (length < sizeof(Header) || length > sizeof(Header) + kMaxPayloadBytes) {
    return record;
  }

  std::vector<uint8_t> bytes(length);
  if (preferences.getBytes("snapshot", bytes.data(), bytes.size()) !=
      bytes.size()) {
    return record;
  }
  Header header{};
  memcpy(&header, bytes.data(), sizeof(header));
  if (header.magic != kMagic || header.schema != kSchema ||
      header.payload_length != length - sizeof(header)) {
    return record;
  }
  const uint8_t* payload = bytes.data() + sizeof(header);
  if (crc32(payload, header.payload_length) != header.crc32) return record;

  record.payload.reserve(header.payload_length + 1);
  record.payload.concat(reinterpret_cast<const char*>(payload),
                        header.payload_length);
  record.stored_at = header.stored_at;
  record.valid = true;
  return record;
}

bool UsageSnapshotStore::load(CodexUsageState& state) {
  Preferences preferences;
  // The namespace does not exist before the first successful snapshot.
  if (!preferences.begin("epd_usage", false)) return false;
  const Record record = readRecord(preferences);
  preferences.end();
  if (!record.valid) return false;

  JsonDocument document;
  if (deserializeJson(document, record.payload) != DeserializationError::Ok) {
    return false;
  }
  CodexUsageState parsed;
  parsed.has_data = document["has_data"] | false;
  parsed.allowed = document["allowed"] | true;
  parsed.limit_reached = document["limit_reached"] | false;
  parsed.plan_type = document["plan_type"] | "--";
  parsed.synced_at = document["synced_at"] | 0ULL;
  parsed.five_hour = windowFromJson(document["five_hour"]);
  parsed.weekly = windowFromJson(document["weekly"]);
  parsed.unknown = windowFromJson(document["unknown"]);
  JsonArrayConst additional = document["additional"].as<JsonArrayConst>();
  size_t index = 0;
  for (JsonVariantConst value : additional) {
    if (index >= 2) break;
    AdditionalRateLimit& item = parsed.additional[index++];
    item.present = value["present"] | false;
    item.name = value["name"] | "";
    item.metered_feature = value["metered_feature"] | "";
    item.primary = windowFromJson(value["primary"]);
    item.secondary = windowFromJson(value["secondary"]);
  }
  if (!parsed.has_data ||
      (!parsed.five_hour.present && !parsed.weekly.present &&
       !parsed.unknown.present)) {
    return false;
  }
  parsed.status = SyncStatus::kOk;
  parsed.status_detail = "cached";
  state = parsed;
  return true;
}

bool UsageSnapshotStore::saveIfDue(const CodexUsageState& state, uint64_t now,
                                   String& error) {
  if (!state.has_data || state.status != SyncStatus::kOk || now < 1700000000ULL) {
    return true;
  }

  Preferences preferences;
  if (!preferences.begin("epd_usage", false)) {
    error = "cannot open usage snapshot namespace";
    return false;
  }
  const Record previous = readRecord(preferences);
  if (previous.valid &&
      (now < previous.stored_at ||
       now - previous.stored_at < kMinWriteIntervalSec)) {
    preferences.end();
    return true;
  }

  JsonDocument document;
  document["has_data"] = state.has_data;
  document["allowed"] = state.allowed;
  document["limit_reached"] = state.limit_reached;
  document["plan_type"] = state.plan_type;
  document["synced_at"] = state.synced_at;
  windowToJson(state.five_hour, document["five_hour"].to<JsonObject>());
  windowToJson(state.weekly, document["weekly"].to<JsonObject>());
  windowToJson(state.unknown, document["unknown"].to<JsonObject>());
  JsonArray additional = document["additional"].to<JsonArray>();
  for (const AdditionalRateLimit& source : state.additional) {
    if (!source.present) continue;
    JsonObject item = additional.add<JsonObject>();
    item["present"] = true;
    item["name"] = source.name;
    item["metered_feature"] = source.metered_feature;
    windowToJson(source.primary, item["primary"].to<JsonObject>());
    windowToJson(source.secondary, item["secondary"].to<JsonObject>());
  }

  String payload;
  serializeJson(document, payload);
  if (payload.length() > kMaxPayloadBytes) {
    preferences.end();
    error = "usage snapshot is too large";
    return false;
  }
  Header header{kMagic, kSchema, 0, now,
                static_cast<uint32_t>(payload.length()),
                crc32(reinterpret_cast<const uint8_t*>(payload.c_str()),
                      payload.length())};
  std::vector<uint8_t> bytes(sizeof(header) + payload.length());
  memcpy(bytes.data(), &header, sizeof(header));
  memcpy(bytes.data() + sizeof(header), payload.c_str(), payload.length());
  const bool ok = preferences.putBytes("snapshot", bytes.data(), bytes.size()) ==
                  bytes.size();
  preferences.end();
  if (!ok) error = "cannot write usage snapshot";
  return ok;
}

bool UsageSnapshotStore::erase(String& error) {
  Preferences preferences;
  if (!preferences.begin("epd_usage", false)) {
    error = "cannot open usage snapshot namespace";
    return false;
  }
  const bool ok = preferences.clear();
  preferences.end();
  if (!ok) error = "cannot erase usage snapshot";
  return ok;
}

}  // namespace epd
