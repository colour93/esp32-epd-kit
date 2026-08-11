#pragma once

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <lvgl.h>

#include "toolkit/app.h"
#include "toolkit/config.h"
#include "toolkit/core_logic.h"
#include "toolkit/hardware.h"

namespace epd {

enum class PresentResult : uint8_t { kNoChange, kPartial, kFull };

class DisplayManager {
 public:
  DisplayManager();

  void begin();
  void renderPage(IPage& page, const PageContext& context,
                  uint32_t page_identity_hash);
  bool renderTimedRegion(IPage& page, const TimedRegion& region,
                         const RuntimeContext& context,
                         uint32_t page_identity_hash);
  void renderPageDiagnostic(const String& page_id);
  void renderPairing(uint32_t passkey, bool configured, bool connected = false);
  void renderLowBattery(uint16_t millivolts, bool connected = false);
  void renderFactoryResetConfirmation(uint32_t code, bool connected = true);
  PresentResult present(const DisplaySettings& settings, bool force_full = false);
  bool oldFrameValid() const;
  uint32_t retainedPageHash() const;
  bool lowBatteryLatched() const;
  void setLowBatteryLatched(bool latched);

 private:
  static constexpr uint16_t kDrawLines = 16;
  static constexpr size_t kDrawBufferBytes =
      8U + hardware::kLogicalStride * kDrawLines;

  struct RegionFlushTarget {
    DisplayManager* display;
    Rect bounds;
  };

  static void flushCallback(lv_display_t* display, const lv_area_t* area,
                            uint8_t* pixel_map);
  static void regionFlushCallback(lv_display_t* display, const lv_area_t* area,
                                  uint8_t* pixel_map);
  static uint32_t tickCallback();
  static uint32_t frameCrc(const uint8_t* frame, size_t length);
  static void logicalToNative(const uint8_t* logical, uint8_t* native);
  static core::Rect logicalToNativeRect(const core::Rect& logical);
  static bool logicalPixel(const uint8_t* frame, uint16_t x, uint16_t y);
  static void writeLogicalPixel(uint8_t* frame, uint16_t x, uint16_t y,
                                bool white);
  static lv_obj_t* addText(lv_obj_t* parent, const char* text, int16_t x,
                           int16_t y, const lv_font_t* font);

  SPIClass spi_;
  GxEPD2_213_B74 panel_;
  lv_display_t* lv_display_ = nullptr;
  uint8_t draw_buffer_[kDrawBufferBytes]{};
  uint8_t region_draw_buffer_[kDrawBufferBytes]{};
  uint8_t frame_[hardware::kLogicalFrameBytes]{};
  uint8_t native_new_[hardware::kNativeFrameBytes]{};
  uint8_t native_old_[hardware::kNativeFrameBytes]{};
  uint32_t pending_page_hash_ = 0;
};

}  // namespace epd
