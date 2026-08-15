#include "toolkit/display_manager.h"

#include <esp_attr.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "toolkit/ui_fonts.h"
#include "toolkit/log.h"

namespace epd {
namespace {

constexpr uint8_t displaySpiBus() {
#if CONFIG_IDF_TARGET_ESP32C3
  // ESP32-C3 exposes one general-purpose SPI controller. In the Arduino core
  // it is FSPI (bus 0); HSPI is bus 1 and SPIClass::begin() rejects it.
  return FSPI;
#else
  return HSPI;
#endif
}

struct RtcDisplayState {
  uint32_t magic;
  uint32_t ui_version;
  uint32_t frame_crc;
  uint16_t partial_count;
  uint16_t reserved;
  uint64_t last_full_at;
  uint32_t page_identity_hash;
  bool low_battery_latched;
  uint8_t frame[hardware::kLogicalFrameBytes];
};

RTC_DATA_ATTR RtcDisplayState g_rtc_display_state{};

void styleScreen(lv_obj_t* root) {
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);

  lv_obj_t* accent = lv_obj_create(root);
  lv_obj_set_pos(accent, 0, 0);
  lv_obj_set_size(accent, 8, hardware::kLogicalHeight);
  lv_obj_set_style_bg_color(accent, lv_color_black(), 0);
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_pad_all(accent, 0, 0);
}

void addRule(lv_obj_t* root, int16_t y) {
  lv_obj_t* rule = lv_obj_create(root);
  lv_obj_set_pos(rule, 18, y);
  lv_obj_set_size(rule, hardware::kLogicalWidth - 26, 1);
  lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_pad_all(rule, 0, 0);
}

void addConnectionIndicator(lv_obj_t* root, bool connected) {
  lv_obj_t* indicator = lv_obj_create(root);
  lv_obj_set_size(indicator, 7, 7);
  lv_obj_align(indicator, LV_ALIGN_TOP_RIGHT, -8, 9);
  lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(indicator, lv_color_black(), 0);
  lv_obj_set_style_border_width(indicator, 1, 0);
  lv_obj_set_style_bg_color(indicator, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(indicator, connected ? LV_OPA_COVER : LV_OPA_TRANSP,
                          0);
  lv_obj_set_style_pad_all(indicator, 0, 0);
}

}  // namespace

DisplayManager::DisplayManager()
    : spi_(displaySpiBus()),
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

uint32_t DisplayManager::retainedPageHash() const {
  return oldFrameValid() ? g_rtc_display_state.page_identity_hash : 0;
}

bool DisplayManager::lowBatteryLatched() const {
  return g_rtc_display_state.low_battery_latched;
}

void DisplayManager::setLowBatteryLatched(bool latched) {
  g_rtc_display_state.low_battery_latched = latched;
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

void DisplayManager::regionFlushCallback(lv_display_t* display,
                                         const lv_area_t* area,
                                         uint8_t* pixel_map) {
  auto* target =
      static_cast<RegionFlushTarget*>(lv_display_get_user_data(display));
  pixel_map += 8;
  const uint16_t area_width = area->x2 - area->x1 + 1;
  const uint16_t area_height = area->y2 - area->y1 + 1;
  const uint16_t area_stride = (area_width + 7U) / 8U;
  for (uint16_t y = 0; y < area_height; ++y) {
    for (uint16_t x = 0; x < area_width; ++x) {
      const size_t source_index = static_cast<size_t>(y) * area_stride + x / 8U;
      const bool white =
          (pixel_map[source_index] & (0x80U >> (x % 8U))) != 0;
      writeLogicalPixel(target->display->frame_,
                        target->bounds.x + area->x1 + x,
                        target->bounds.y + area->y1 + y, white);
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
  TOOLKIT_LOG("display", String("LVGL ready ") + hardware::kLogicalWidth + "x" +
                             hardware::kLogicalHeight + " panel=" +
                             hardware::kPanelName);
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

void DisplayManager::renderPage(IPage& page, const PageContext& context,
                                uint32_t page_identity_hash) {
  TOOLKIT_LOG("display", String("render page=") + page.manifest().id);
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  page.buildUi(root, context);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
  pending_page_hash_ = page_identity_hash;
}

bool DisplayManager::renderTimedRegion(IPage& page, const TimedRegion& region,
                                       const RuntimeContext& context,
                                       uint32_t page_identity_hash) {
  if (!oldFrameValid() || retainedPageHash() != page_identity_hash ||
      region.bounds.empty() || region.bounds.x < 0 || region.bounds.y < 0 ||
      region.bounds.x + region.bounds.width > hardware::kLogicalWidth ||
      region.bounds.y + region.bounds.height > hardware::kLogicalHeight) {
    return false;
  }
  memcpy(frame_, g_rtc_display_state.frame, sizeof(frame_));
  RegionFlushTarget target{this, region.bounds};
  lv_display_t* region_display =
      lv_display_create(region.bounds.width, region.bounds.height);
  if (region_display == nullptr) return false;
  lv_display_set_user_data(region_display, &target);
  lv_display_set_color_format(region_display, LV_COLOR_FORMAT_I1);
  lv_display_set_flush_cb(region_display, regionFlushCallback);
  lv_display_set_buffers(region_display, region_draw_buffer_, nullptr,
                         sizeof(region_draw_buffer_),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(region_display);
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  page.buildTimedRegion(region.id, root, context);
  lv_obj_invalidate(root);
  lv_refr_now(region_display);
  lv_display_set_default(lv_display_);
  lv_display_delete(region_display);
  pending_page_hash_ = page_identity_hash;
  TOOLKIT_LOG("display", String("render timed region=") + region.id + " page=" +
                             page.manifest().id);
  return true;
}

void DisplayManager::renderPageDiagnostic(const String& page_id) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  styleScreen(root);
  addText(root, "页面不可用", 18, 10, &ui_font_zh_16);
  addRule(root, 37);
  addText(root, "未知 page id", 18, 46, &ui_font_zh_14);
  addText(root, page_id.c_str(), 18, 68, &lv_font_montserrat_14);
  addText(root, "请通过管理端重新配置", 18,
          hardware::kLogicalHeight - 21, &ui_font_zh_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
  pending_page_hash_ = 0;
}

void DisplayManager::renderPairing(uint32_t passkey, bool configured,
                                   bool connected) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  styleScreen(root);
  addText(root, configured ? "配置模式" : "初次设置", 18, 6,
          &ui_font_zh_16);
  lv_obj_t* ble = addText(root, "BLE", 0, 8, &lv_font_montserrat_12);
  lv_obj_align(ble, LV_ALIGN_TOP_RIGHT, -22, 8);
  addConnectionIndicator(root, connected);
  addRule(root, 31);
  addText(root, "蓝牙配对码", 18, 38, &ui_font_zh_14);
  char value[12];
  snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(passkey));
  lv_obj_t* key = addText(root, value, 18, 52, &lv_font_montserrat_36);
  lv_obj_set_style_text_letter_space(key, 4, 0);
  addText(root, "请在 EPD Agent 配对时输入以上号码", 18,
          hardware::kLogicalHeight - 21, &ui_font_zh_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
  pending_page_hash_ = 0;
}

void DisplayManager::renderLowBattery(uint16_t millivolts, bool connected) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  styleScreen(root);
  addText(root, "电量过低", 18, 9, &ui_font_zh_16);
  addConnectionIndicator(root, connected);
  char value[16];
  snprintf(value, sizeof(value), "%.2fV", millivolts / 1000.0F);
  addText(root, value, 18, 39, &lv_font_montserrat_36);
  addText(root, "无线连接已暂停 充电后自动恢复", 18,
          hardware::kLogicalHeight - 26,
          &ui_font_zh_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
  pending_page_hash_ = 0;
}

void DisplayManager::renderFactoryResetConfirmation(uint32_t code,
                                                     bool connected) {
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  styleScreen(root);
  addText(root, "恢复出厂设置?", 18, 10, &ui_font_zh_16);
  addConnectionIndicator(root, connected);
  addRule(root, 37);
  char value[12];
  snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(code));
  addText(root, value, 18, 43, &lv_font_montserrat_36);
  addText(root, "在受信主机输入确认码", 18,
          hardware::kLogicalHeight - 28, &ui_font_zh_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
  pending_page_hash_ = 0;
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
  if (dirty.empty() && !full) {
    TOOLKIT_LOG("display", "frame unchanged");
    return PresentResult::kNoChange;
  }
  TOOLKIT_LOG("display", String("present mode=") + (full ? "full" : "partial") +
                             " dirty=" + dirty.x + "," + dirty.y + "," +
                             dirty.width + "x" + dirty.height +
                             " partial_count=" +
                             g_rtc_display_state.partial_count);

  logicalToNative(frame_, native_new_);
  if (old_valid) logicalToNative(g_rtc_display_state.frame, native_old_);

#if CONFIG_IDF_TARGET_ESP32C3
  // The AirM2M variant provides the working E029A01 FSPI pin map, including
  // its unused MISO pin. Calling begin() without it makes this Arduino core
  // emit a misleading "SPI Does not have default pins" diagnostic.
  spi_.begin();
#else
  spi_.begin(hardware::kSpiSck, -1, hardware::kSpiMosi, hardware::kEpdCs);
#endif
  panel_.selectSPI(spi_, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  panel_.init(0, full, 10, false);

  if (full) {
#if defined(EPD_PANEL_E029A01)
    panel_.writeImage(native_new_, 0, 0, hardware::kNativeWidth,
                      hardware::kNativeHeight);
#else
    panel_.writeImageForFullRefresh(native_new_, 0, 0, hardware::kNativeWidth,
                                    hardware::kNativeHeight);
#endif
    panel_.refresh(false);
#if defined(EPD_PANEL_E029A01)
    panel_.writeImage(native_new_, 0, 0, hardware::kNativeWidth,
                      hardware::kNativeHeight);
#else
    panel_.writeImageAgain(native_new_, 0, 0, hardware::kNativeWidth,
                           hardware::kNativeHeight);
#endif
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
#if defined(EPD_PANEL_E029A01)
    panel_.writeImagePart(native_new_, native_dirty.x, native_dirty.y,
                          hardware::kNativeWidth, hardware::kNativeHeight,
                          native_dirty.x, native_dirty.y, native_dirty.width,
                          native_dirty.height);
#else
    panel_.writeImagePartAgain(native_new_, native_dirty.x, native_dirty.y,
                               hardware::kNativeWidth, hardware::kNativeHeight,
                               native_dirty.x, native_dirty.y,
                               native_dirty.width, native_dirty.height);
#endif
  }

  panel_.hibernate();
  panel_.end();

  memcpy(g_rtc_display_state.frame, frame_, sizeof(frame_));
  g_rtc_display_state.magic = hardware::kRtcMagic;
  g_rtc_display_state.ui_version = hardware::kDisplayUiVersion;
  g_rtc_display_state.frame_crc = frameCrc(frame_, sizeof(frame_));
  g_rtc_display_state.page_identity_hash = pending_page_hash_;
  if (full) {
    g_rtc_display_state.partial_count = 0;
    if (now >= 1700000000ULL) g_rtc_display_state.last_full_at = now;
  } else {
    ++g_rtc_display_state.partial_count;
  }
  TOOLKIT_LOG("display", String("present complete mode=") +
                             (full ? "full" : "partial"));
  return full ? PresentResult::kFull : PresentResult::kPartial;
}

}  // namespace epd
