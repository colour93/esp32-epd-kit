#include "toolkit/display_manager.h"

#include <esp_attr.h>
#include <time.h>

#include <algorithm>
#include <cstring>

namespace epd {
namespace {

struct RtcDisplayState {
  uint32_t magic;
  uint32_t ui_version;
  uint32_t frame_crc;
  uint16_t partial_count;
  uint16_t reserved;
  uint64_t last_full_at;
  bool low_battery_latched;
  uint8_t frame[hardware::kLogicalFrameBytes];
};

RTC_DATA_ATTR RtcDisplayState g_rtc_display_state{};

}  // namespace

DisplayManager::DisplayManager()
    : spi_(HSPI),
      panel_(hardware::kEpdCs, hardware::kEpdDc, hardware::kEpdReset,
             hardware::kEpdBusy) {}

uint32_t DisplayManager::tickCallback() { return millis(); }

uint32_t DisplayManager::frameCrc(const uint8_t* frame, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= frame[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

bool DisplayManager::oldFrameValid() const {
  return g_rtc_display_state.magic == hardware::kRtcMagic &&
         g_rtc_display_state.ui_version == hardware::kDisplayUiVersion &&
         g_rtc_display_state.frame_crc ==
             frameCrc(g_rtc_display_state.frame, sizeof(g_rtc_display_state.frame));
}

bool DisplayManager::logicalPixel(const uint8_t* frame, uint16_t x, uint16_t y) {
  const size_t index = static_cast<size_t>(y) * hardware::kLogicalStride + x / 8U;
  return (frame[index] & (0x80U >> (x % 8U))) != 0;
}

void DisplayManager::writeLogicalPixel(uint8_t* frame, uint16_t x, uint16_t y,
                                       bool white) {
  if (x >= hardware::kLogicalWidth || y >= hardware::kLogicalHeight) return;
  const size_t index = static_cast<size_t>(y) * hardware::kLogicalStride + x / 8U;
  const uint8_t mask = 0x80U >> (x % 8U);
  if (white) {
    frame[index] |= mask;
  } else {
    frame[index] &= ~mask;
  }
}

void DisplayManager::flushCallback(lv_display_t* display, const lv_area_t* area,
                                   uint8_t* pixel_map) {
  auto* self = static_cast<DisplayManager*>(lv_display_get_user_data(display));
  pixel_map += 8;  // LVGL's two-entry I1 palette.
  const uint16_t area_width = area->x2 - area->x1 + 1;
  const uint16_t area_height = area->y2 - area->y1 + 1;
  const uint16_t area_stride = (area_width + 7U) / 8U;

  for (uint16_t y = 0; y < area_height; ++y) {
    for (uint16_t x = 0; x < area_width; ++x) {
      const size_t source_index = static_cast<size_t>(y) * area_stride + x / 8U;
      const bool white = (pixel_map[source_index] & (0x80U >> (x % 8U))) != 0;
      writeLogicalPixel(self->frame_, area->x1 + x, area->y1 + y, white);
    }
  }
  lv_display_flush_ready(display);
}

void DisplayManager::begin() {
  memset(frame_, 0xFF, sizeof(frame_));
  lv_init();
  lv_tick_set_cb(tickCallback);
  lv_display_ = lv_display_create(hardware::kLogicalWidth, hardware::kLogicalHeight);
  lv_display_set_user_data(lv_display_, this);
  lv_display_set_color_format(lv_display_, LV_COLOR_FORMAT_I1);
  lv_display_set_flush_cb(lv_display_, flushCallback);
  lv_display_set_buffers(lv_display_, draw_buffer_, nullptr, sizeof(draw_buffer_),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
}

lv_obj_t* DisplayManager::addText(lv_obj_t* parent, const char* text, int16_t x,
                                  int16_t y, const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_black(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_pos(label, x, y);
  return label;
}

void DisplayManager::renderApp(IApp& app) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  app.buildUi(root, app.state());
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
}

void DisplayManager::renderPairing(uint32_t passkey, bool configured) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  addText(root, configured ? "CONFIG MODE" : "FIRST SETUP", 5, 4,
          &lv_font_montserrat_20);
  addText(root, "BLE passkey", 5, 35, &lv_font_montserrat_14);
  char value[12];
  snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(passkey));
  lv_obj_t* key = addText(root, value, 5, 54, &lv_font_montserrat_20);
  lv_obj_set_style_text_letter_space(key, 5, 0);
  addText(root, "Subscribe TX, then send hello", 5, 91,
          &lv_font_montserrat_12);
  addText(root, configured ? "Physical key opened this session"
                           : "Pair using the passkey above",
          5, 106, &lv_font_montserrat_12);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
}

void DisplayManager::renderLowBattery(uint16_t millivolts) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  addText(root, "LOW BATTERY", 5, 9, &lv_font_montserrat_20);
  char value[32];
  snprintf(value, sizeof(value), "%.2fV - radio disabled", millivolts / 1000.0F);
  addText(root, value, 5, 49, &lv_font_montserrat_16);
  addText(root, "Charge battery to resume updates", 5, 85,
          &lv_font_montserrat_12);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
}

void DisplayManager::renderFactoryResetConfirmation() {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  addText(root, "FACTORY RESET?", 5, 10, &lv_font_montserrat_20);
  addText(root, "BLE prepare: hold KEY for 2 sec", 5, 52,
          &lv_font_montserrat_14);
  addText(root, "Then commit nonce within 30 sec", 5, 80,
          &lv_font_montserrat_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
}

void DisplayManager::logicalToNative(const uint8_t* logical, uint8_t* native) {
  memset(native, 0xFF, hardware::kNativeFrameBytes);
  for (uint16_t y = 0; y < hardware::kLogicalHeight; ++y) {
    for (uint16_t x = 0; x < hardware::kLogicalWidth; ++x) {
      if (logicalPixel(logical, x, y)) continue;
      const uint16_t native_x = hardware::kNativeVisibleWidth - y - 1U;
      const uint16_t native_y = x;
      const size_t index = static_cast<size_t>(native_y) * hardware::kNativeStride +
                           native_x / 8U;
      native[index] &= ~(0x80U >> (native_x % 8U));
    }
  }
}

core::Rect DisplayManager::logicalToNativeRect(const core::Rect& logical) {
  if (logical.empty()) return {};
  int16_t x1 =
      hardware::kNativeVisibleWidth - (logical.y + logical.height);
  int16_t x2 = hardware::kNativeVisibleWidth - logical.y - 1;
  x1 &= ~0x7;
  x2 = std::min<int16_t>(hardware::kNativeWidth - 1, x2 | 0x7);
  return {x1, logical.x, static_cast<int16_t>(x2 - x1 + 1), logical.width};
}

PresentResult DisplayManager::present(const DisplaySettings& settings,
                                      bool force_full) {
  const bool old_valid = oldFrameValid();
  const uint8_t* old_frame = old_valid ? g_rtc_display_state.frame : frame_;
  const core::Rect dirty = old_valid
                               ? core::findDirtyRect(old_frame, frame_,
                                                     hardware::kLogicalWidth,
                                                     hardware::kLogicalHeight)
                               : core::Rect{0, 0, hardware::kLogicalWidth,
                                            hardware::kLogicalHeight};
  const uint64_t now = static_cast<uint64_t>(time(nullptr));
  const uint32_t full_age =
      now >= 1700000000ULL && g_rtc_display_state.last_full_at > 0 &&
              now > g_rtc_display_state.last_full_at
          ? static_cast<uint32_t>(std::min<uint64_t>(
                UINT32_MAX, now - g_rtc_display_state.last_full_at))
          : 0;
  const bool full = force_full || core::shouldFullRefresh(
                                      old_valid, dirty, hardware::kLogicalWidth,
                                      hardware::kLogicalHeight,
                                      settings.full_area_threshold_percent,
                                      g_rtc_display_state.partial_count,
                                      settings.full_after_partial_count, full_age,
                                      settings.full_max_age_sec);
  if (dirty.empty() && !full) return PresentResult::kNoChange;

  logicalToNative(frame_, native_new_);
  if (old_valid) logicalToNative(g_rtc_display_state.frame, native_old_);

  spi_.begin(hardware::kSpiSck, -1, hardware::kSpiMosi, hardware::kEpdCs);
  panel_.selectSPI(spi_, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  panel_.init(0, full, 10, false);

  if (full) {
    panel_.writeImageForFullRefresh(native_new_, 0, 0, hardware::kNativeWidth,
                                    hardware::kNativeHeight);
    panel_.refresh(false);
    panel_.writeImageAgain(native_new_, 0, 0, hardware::kNativeWidth,
                           hardware::kNativeHeight);
  } else {
    const core::Rect native_dirty = logicalToNativeRect(dirty);
    panel_.writeImagePartToPrevious(
        native_old_, native_dirty.x, native_dirty.y, hardware::kNativeWidth,
        hardware::kNativeHeight, native_dirty.x, native_dirty.y,
        native_dirty.width, native_dirty.height);
    panel_.writeImagePart(native_new_, native_dirty.x, native_dirty.y,
                          hardware::kNativeWidth, hardware::kNativeHeight,
                          native_dirty.x, native_dirty.y, native_dirty.width,
                          native_dirty.height);
    panel_.refresh(native_dirty.x, native_dirty.y, native_dirty.width,
                   native_dirty.height);
    panel_.writeImagePartAgain(native_new_, native_dirty.x, native_dirty.y,
                               hardware::kNativeWidth, hardware::kNativeHeight,
                               native_dirty.x, native_dirty.y,
                               native_dirty.width, native_dirty.height);
  }

  panel_.hibernate();
  panel_.end();

  memcpy(g_rtc_display_state.frame, frame_, sizeof(frame_));
  g_rtc_display_state.magic = hardware::kRtcMagic;
  g_rtc_display_state.ui_version = hardware::kDisplayUiVersion;
  g_rtc_display_state.frame_crc = frameCrc(frame_, sizeof(frame_));
  if (full) {
    g_rtc_display_state.partial_count = 0;
    if (now >= 1700000000ULL) g_rtc_display_state.last_full_at = now;
  } else {
    ++g_rtc_display_state.partial_count;
  }
  return full ? PresentResult::kFull : PresentResult::kPartial;
}

}  // namespace epd
