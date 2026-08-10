#include "toolkit/codex_usage_app.h"

#include <time.h>

#include "toolkit/ui_fonts.h"

namespace epd {
namespace {

lv_obj_t* label(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                const lv_font_t* font = &lv_font_montserrat_14) {
  lv_obj_t* object = lv_label_create(parent);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_color(object, lv_color_black(), 0);
  lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_pos(object, x, y);
  return object;
}

void rule(lv_obj_t* parent, int16_t x, int16_t y, int16_t width) {
  lv_obj_t* object = lv_obj_create(parent);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, width, 1);
  lv_obj_set_style_bg_color(object, lv_color_black(), 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
}

void progress(lv_obj_t* parent, int16_t x, int16_t y, int16_t width,
              uint8_t value) {
  lv_obj_t* bar = lv_bar_create(parent);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, width, 8);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
}

void connectionIndicator(lv_obj_t* parent, bool connected) {
  lv_obj_t* indicator = lv_obj_create(parent);
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

uint8_t batteryPercent(uint16_t millivolts) {
  if (millivolts <= 3300) return 0;
  if (millivolts >= 4200) return 100;
  return static_cast<uint8_t>((millivolts - 3300U) / 9U);
}

String planName(const String& value) {
  String normalized = value;
  normalized.toLowerCase();
  normalized.replace("_", "");
  normalized.replace("-", "");
  normalized.replace(" ", "");
  if (normalized.indexOf("plus") >= 0) return "GPT Plus";
  if (normalized == "pro" || normalized.indexOf("chatgptpro") >= 0) {
    return "GPT Pro";
  }
  if (normalized.indexOf("free") >= 0) return "GPT Free";
  if (normalized.indexOf("team") >= 0) return "GPT Team";
  if (normalized.indexOf("business") >= 0) return "GPT Business";
  if (normalized.indexOf("enterprise") >= 0) return "GPT Enterprise";
  if (normalized.isEmpty() || normalized == "unknown" || normalized == "--") {
    return "GPT --";
  }
  return value;
}

RateLimitWindow parseWindow(JsonVariantConst value) {
  RateLimitWindow window;
  if (!value.is<JsonObjectConst>() || !value["used_percent"].is<uint8_t>()) {
    return window;
  }
  window.present = true;
  window.used_percent = value["used_percent"].as<uint8_t>();
  window.window_duration_mins = value["window_duration_mins"] | 0U;
  window.resets_at = value["resets_at"] | 0ULL;
  return window;
}

const char* statusText(SyncStatus status) {
  switch (status) {
    case SyncStatus::kWaiting: return "等待数据";
    case SyncStatus::kFresh: return "同步正常";
    case SyncStatus::kStale: return "数据过期";
    case SyncStatus::kAuthRequired: return "请登录 Codex";
    case SyncStatus::kUnavailable: return "服务离线";
    case SyncStatus::kInvalid: return "数据异常";
    case SyncStatus::kLowBattery: return "电量低";
  }
  return "数据异常";
}

const char* windowLabel(const RateLimitWindow& window) {
  if (!window.present || window.window_duration_mins == 0) return "未知";
  if (window.window_duration_mins == 7U * 24U * 60U) return "7 天";
  if (window.window_duration_mins == 5U * 60U) return "5 小时";
  return "未知";
}

}  // namespace

CodexUsageState CodexUsageRenderer::parse(const ResourceRecord* resource,
                                          uint64_t now) {
  CodexUsageState state;
  if (resource == nullptr) return state;
  if (resource->schema_id != "codex.rate_limits" ||
      resource->schema_version != 1) {
    state.status = SyncStatus::kInvalid;
    return state;
  }
  JsonVariantConst payload = resource->payload.as<JsonVariantConst>();
  const String source_status = payload["source_status"] | "ok";
  if (source_status == "auth_required") {
    state.status = SyncStatus::kAuthRequired;
  } else if (source_status != "ok") {
    state.status = SyncStatus::kUnavailable;
  } else if (resource->ttl_sec > 0 && now > resource->updated_at &&
             now - resource->updated_at > resource->ttl_sec) {
    state.status = SyncStatus::kStale;
  } else {
    state.status = SyncStatus::kFresh;
  }
  state.updated_at = resource->updated_at;
  state.plan_type = payload["plan_type"] | "--";
  state.limit_reached = payload["limit_reached"] | false;
  JsonVariantConst selected = payload["selected"];
  if (!selected.is<JsonObjectConst>()) {
    if (state.status == SyncStatus::kFresh) state.status = SyncStatus::kInvalid;
    return state;
  }
  state.limit_name = selected["limit_name"] | "Codex";
  state.primary = parseWindow(selected["primary"]);
  state.secondary = parseWindow(selected["secondary"]);
  state.has_data = state.primary.present || state.secondary.present;
  if (!state.has_data && state.status == SyncStatus::kFresh) {
    state.status = SyncStatus::kInvalid;
  }
  return state;
}

String CodexUsageRenderer::resetText(const RateLimitWindow& window,
                                     uint64_t now) {
  if (!window.present || window.resets_at == 0) return "--";
  const uint64_t remaining = window.resets_at > now ? window.resets_at - now : 0;
  const uint32_t hours = remaining / 3600U;
  const uint32_t minutes = (remaining % 3600U) / 60U;
  char text[20];
  if (hours >= 24) {
    snprintf(text, sizeof(text), "%lu天%lu时",
             static_cast<unsigned long>(hours / 24U),
             static_cast<unsigned long>(hours % 24U));
  } else {
    snprintf(text, sizeof(text), "%lu时%lu分",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  }
  return text;
}

void CodexUsageRenderer::buildUi(lv_obj_t* root,
                                 const ResourceRecord* resource,
                                 const RenderContext& context) {
  const CodexUsageState state = parse(resource, context.now);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);

  label(root, "Codex", 8, 3, &lv_font_montserrat_16);
  label(root, "-", 59, 5, &lv_font_montserrat_12);
  const String plan = planName(state.plan_type);
  label(root, plan.c_str(), 69, 5, &lv_font_montserrat_12);
  if (context.battery_enabled) {
    char power[8];
    if (context.battery_mv >= 2500) {
      snprintf(power, sizeof(power), "%u%%", batteryPercent(context.battery_mv));
    } else {
      snprintf(power, sizeof(power), "--%%");
    }
    lv_obj_t* power_label = label(root, power, 0, 5, &lv_font_montserrat_12);
    lv_obj_align(power_label, LV_ALIGN_TOP_RIGHT, -22, 5);
  }
  connectionIndicator(root, context.connected);
  rule(root, 8, 25, 234);

  const RateLimitWindow windows[2] = {state.primary, state.secondary};
  const int16_t origins[2] = {8, 130};
  for (uint8_t index = 0; index < 2; ++index) {
    const RateLimitWindow& window = windows[index];
    const int16_t x = origins[index];
    label(root, windowLabel(window), x, 31, &ui_font_zh_14);
    char percent[8];
    if (window.present) {
      snprintf(percent, sizeof(percent), "%u%%", window.remainingPercent());
    } else {
      snprintf(percent, sizeof(percent), "--%%");
    }
    label(root, percent, x, 45, &lv_font_montserrat_28);
    progress(root, x, 76, 111, window.present ? window.remainingPercent() : 0);
    const String reset = resetText(window, context.now);
    label(root, reset.c_str(), x, 85, &ui_font_zh_14);
  }

  rule(root, 8, 102, 234);
  lv_obj_t* status = label(root, statusText(state.status), 8, 105, &ui_font_zh_14);
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_set_style_bg_color(status, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(status, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(status, 4, 0);
  lv_obj_set_style_pad_right(status, 4, 0);
  char updated[24] = "更新 --:--";
  if (state.updated_at > 0) {
    const time_t timestamp = static_cast<time_t>(
        state.updated_at + static_cast<int64_t>(context.utc_offset_minutes) * 60);
    struct tm local_time {};
    gmtime_r(&timestamp, &local_time);
    strftime(updated, sizeof(updated), "更新 %m-%d %H:%M", &local_time);
  }
  lv_obj_t* updated_label = label(root, updated, 0, 105, &ui_font_zh_14);
  lv_obj_align(updated_label, LV_ALIGN_TOP_RIGHT, -8, 105);
}

}  // namespace epd
