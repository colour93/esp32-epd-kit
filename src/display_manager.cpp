#include "toolkit/display_manager.h"

#include <esp_attr.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "toolkit/ui_fonts.h"
#include "toolkit/log.h"

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
  lv_obj_set_size(rule, 224, 1);
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
                             hardware::kLogicalHeight);
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

void DisplayManager::renderView(IRenderer& renderer,
                                const ResourceRecord* resource,
                                const RenderContext& context) {
  TOOLKIT_LOG("display", String("render renderer=") + renderer.id() +
                             " resource=" +
                             (resource == nullptr ? "missing" : resource->key));
  lv_obj_t* root = lv_screen_active();
  lv_obj_clean(root);
  renderer.buildUi(root, resource, context);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
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
  addText(root, "请在电脑端输入上方号码", 18, 101, &ui_font_zh_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
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
  addText(root, "无线连接已暂停 充电后自动恢复", 18, 96,
          &ui_font_zh_14);
  lv_obj_invalidate(root);
  lv_refr_now(lv_display_);
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
  addText(root, "在受信主机输入确认码", 18, 94, &ui_font_zh_14);
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
  TOOLKIT_LOG("display", String("present complete mode=") +
                             (full ? "full" : "partial"));
  return full ? PresentResult::kFull : PresentResult::kPartial;
}

}  // namespace epd
