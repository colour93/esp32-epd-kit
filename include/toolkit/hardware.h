#pragma once

#include <Arduino.h>

namespace epd::hardware {

#if defined(EPD_PANEL_E029A01)
#if CONFIG_IDF_TARGET_ESP32C3
constexpr int kSpiSck = 2;
constexpr int kSpiMosi = 3;
constexpr int kEpdCs = 7;
constexpr int kEpdBusy = 0;
constexpr int kEpdReset = 10;
constexpr int kEpdDc = 6;
constexpr int kButton1 = 4;
constexpr int kButton2 = 13;
constexpr bool kUserKeyActiveHigh = true;
constexpr int kBatteryAdc = -1;
constexpr bool kHasBatteryAdc = false;
#else
constexpr int kSpiSck = 13;
constexpr int kSpiMosi = 14;
constexpr int kEpdCs = 15;
constexpr int kEpdBusy = 25;
constexpr int kEpdReset = 26;
constexpr int kEpdDc = 27;
constexpr int kButton1 = 12;
constexpr int kButton2 = -1;
constexpr bool kUserKeyActiveHigh = false;
constexpr int kBatteryAdc = 36;
constexpr bool kHasBatteryAdc = true;
#endif

constexpr uint16_t kNativeWidth = 128;
constexpr uint16_t kNativeVisibleWidth = 128;
constexpr uint16_t kNativeHeight = 296;
constexpr uint16_t kLogicalWidth = 296;
constexpr uint16_t kLogicalHeight = 128;
constexpr const char* kPanelName = "E029A01";
#else
constexpr int kSpiSck = 13;
constexpr int kSpiMosi = 14;
constexpr int kEpdCs = 15;
constexpr int kEpdBusy = 25;
constexpr int kEpdReset = 26;
constexpr int kEpdDc = 27;
constexpr int kButton1 = 12;
constexpr int kButton2 = -1;
constexpr bool kUserKeyActiveHigh = false;
constexpr int kBatteryAdc = 36;
constexpr bool kHasBatteryAdc = true;

constexpr uint16_t kNativeWidth = 128;
constexpr uint16_t kNativeVisibleWidth = 122;
constexpr uint16_t kNativeHeight = 250;
constexpr uint16_t kLogicalWidth = 250;
constexpr uint16_t kLogicalHeight = 122;
constexpr const char* kPanelName = "GDEM0213B74";
#endif

constexpr int kUserKey = kButton1;
constexpr uint16_t kLogicalStride = (kLogicalWidth + 7U) / 8U;
constexpr size_t kLogicalFrameBytes = kLogicalStride * kLogicalHeight;
constexpr uint16_t kNativeStride = (kNativeWidth + 7U) / 8U;
constexpr size_t kNativeFrameBytes = kNativeStride * kNativeHeight;

static_assert(kLogicalHeight == kNativeVisibleWidth,
              "logical canvas must fill the visible panel width");
static_assert(kLogicalWidth == kNativeHeight,
              "logical canvas must fill the panel height");
static_assert(kNativeWidth % 8U == 0,
              "native panel width must be byte aligned");

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
constexpr uint32_t kDisplayLayoutRevision = 10;
constexpr uint32_t kDisplayUiVersion =
    versionHash(EPD_TOOLKIT_VERSION) ^ kDisplayLayoutRevision;
constexpr uint32_t kRtcMagic = 0x45504431U;  // "EPD1"

}  // namespace epd::hardware
