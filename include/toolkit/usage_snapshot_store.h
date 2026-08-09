#pragma once

#include <Preferences.h>

#include "toolkit/usage_model.h"

namespace epd {

class UsageSnapshotStore {
 public:
  bool load(CodexUsageState& state);
  bool saveIfDue(const CodexUsageState& state, uint64_t now, String& error);
  bool erase(String& error);

 private:
  struct __attribute__((packed)) Header {
    uint32_t magic;
    uint16_t schema;
    uint16_t reserved;
    uint64_t stored_at;
    uint32_t payload_length;
    uint32_t crc32;
  };

  struct Record {
    bool valid = false;
    uint64_t stored_at = 0;
    String payload;
  };

  static constexpr uint32_t kMagic = 0x55534731U;  // "USG1"
  static constexpr uint16_t kSchema = 1;
  static constexpr uint64_t kMinWriteIntervalSec = 3600;
  static constexpr size_t kMaxPayloadBytes = 4096;

  static uint32_t crc32(const uint8_t* data, size_t length);
  static Record readRecord(Preferences& preferences);
};

}  // namespace epd
