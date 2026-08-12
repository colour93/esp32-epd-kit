#pragma once

#include <Preferences.h>

#include <vector>

#include "toolkit/config.h"

namespace epd {

class ConfigStore {
 public:
  bool load(DeviceConfig& config);
  bool save(const DeviceConfig& config, String& error);
  bool erase(String& error);

 private:
  struct __attribute__((packed)) SlotHeader {
    uint32_t magic;
    uint16_t schema;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_length;
    uint32_t crc32;
  };

  struct SlotData {
    bool valid = false;
    bool msgpack = false;
    uint32_t sequence = 0;
    std::vector<uint8_t> payload;
  };

  static constexpr uint32_t kSlotMagic = 0x43464735U;        // "CFG5"
  static constexpr uint32_t kLegacySlotMagic = 0x43464734U;  // "CFG4"
  static constexpr size_t kMaxPayloadBytes = 4096;
  static constexpr size_t kLegacyMaxPayloadBytes = 8192;

  SlotData readSlot(Preferences& preferences, const char* key);
  static uint32_t crc32(const uint8_t* data, size_t length);
};

}  // namespace epd
