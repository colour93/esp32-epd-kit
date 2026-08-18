#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

namespace epd {

enum class ResourcePersistence : uint8_t { kVolatile, kSnapshot };

struct ResourceRecord {
  String key;
  String schema_id;
  uint16_t schema_version = 0;
  uint64_t revision = 0;
  uint64_t updated_at = 0;
  uint32_t ttl_sec = 0;
  ResourcePersistence persistence = ResourcePersistence::kVolatile;
  uint32_t content_crc = 0;
  JsonDocument payload;
};

enum class ResourcePutResult : uint8_t {
  kCreated,
  kUpdated,
  kUnchanged,
  kConflict,
  kInvalid,
  kFull,
};

class ResourceStore {
 public:
  bool load(String& error);
  ResourcePutResult put(JsonVariantConst value, String& error);
  bool remove(const String& key, String& error);
  const ResourceRecord* get(const String& key) const;
  ResourceRecord* get(const String& key);
  size_t size() const { return records_.size(); }
  const ResourceRecord& at(size_t index) const { return records_[index]; }
  bool saveIfDue(uint64_t now, bool force, String& error);
  bool erase(String& error);

  static void toJson(const ResourceRecord& record, JsonObject out,
                     bool include_payload = true);

 private:
  static constexpr size_t kMaxResources = 16;
  static constexpr size_t kMaxPayloadBytes = 2048;
  static constexpr size_t kMaxSnapshotBytes = 4096;
  static constexpr uint64_t kMinWriteIntervalSec = 3600;
  static constexpr uint32_t kFailedWriteRetryMs = 60000;

  static uint32_t crc32(const uint8_t* data, size_t length);
  static bool parseRecord(JsonVariantConst value, ResourceRecord& out,
                          String& error);
  static uint32_t contentCrc(const ResourceRecord& record);

  std::vector<ResourceRecord> records_;
  uint64_t last_saved_at_ = 0;
  uint32_t last_save_attempt_ms_ = 0;
  bool dirty_ = false;
};

}  // namespace epd
