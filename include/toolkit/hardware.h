#pragma once

#include <Arduino.h>

namespace epd::hardware {

constexpr int kSpiSck = 13;
constexpr int kSpiMosi = 14;
constexpr int kEpdCs = 15;
constexpr int kEpdBusy = 25;
constexpr int kEpdReset = 26;
constexpr int kEpdDc = 27;
constexpr int kUserKey = 12;
constexpr int kBatteryAdc = 36;

constexpr uint16_t kLogicalWidth = 250;
constexpr uint16_t kLogicalHeight = 122;
constexpr uint16_t kNativeWidth = 128;
constexpr uint16_t kNativeVisibleWidth = 122;
constexpr uint16_t kNativeHeight = 250;
constexpr uint16_t kLogicalStride = (kLogicalWidth + 7U) / 8U;
constexpr size_t kLogicalFrameBytes = kLogicalStride * kLogicalHeight;
constexpr uint16_t kNativeStride = kNativeWidth / 8U;
constexpr size_t kNativeFrameBytes = kNativeStride * kNativeHeight;

#ifndef EPD_TOOLKIT_VERSION
#define EPD_TOOLKIT_VERSION "dev"
#endif

constexpr uint32_t versionHash(const char* value,
                               uint32_t hash = 2166136261U) {
  return *value == '\0'
             ? hash
             : versionHash(value + 1,
                           (hash ^ static_cast<uint8_t>(*value)) * 16777619U);
}

// Bump the layout revision when rendering semantics change. Combining it with
// the firmware version also invalidates the RTC frame after a version upgrade.
constexpr uint32_t kDisplayLayoutRevision = 1;
constexpr uint32_t kDisplayUiVersion =
    versionHash(EPD_TOOLKIT_VERSION) ^ kDisplayLayoutRevision;
constexpr uint32_t kRtcMagic = 0x45504431U;  // "EPD1"

}  // namespace epd::hardware
