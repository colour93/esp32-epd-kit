#include "toolkit/codex_usage_app.h"

#include <time.h>

namespace epd {
namespace {

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                    const lv_font_t* font = &lv_font_montserrat_14) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_black(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_pos(label, x, y);
  return label;
}

}  // namespace

AppManifest CodexUsageApp::manifest() const {
  return {"codex_usage", "Codex Usage", "1.0.0", true, true};
}

bool CodexUsageApp::validateConfig(const DeviceConfig& config, String& error) const {
  if (config.codex.account_id.isEmpty()) {
    error = "apps.codex_usage.account_id is required";
    return false;
  }
  if (config.codex.access_token.isEmpty()) {
    error = "apps.codex_usage.access_token is required";
    return false;
  }
  return true;
}

UpdateResult CodexUsageApp::update(AppContext& context) {
  const CodexUsageState previous = state_.usage;
  state_.battery_mv = context.battery_mv;
  const SyncStatus status = context.codex_client.fetch(
      context.config.codex, context.config.device.locale, state_.usage);
  const bool changed = previous.status != state_.usage.status ||
                       previous.five_hour.remainingPercent() !=
                           state_.usage.five_hour.remainingPercent() ||
                       previous.weekly.remainingPercent() !=
                           state_.usage.weekly.remainingPercent() ||
                       previous.plan_type != state_.usage.plan_type;
  return {status, changed, context.config.power.poll_interval_sec};
}

String CodexUsageApp::resetText(const RateLimitWindow& window) {
  if (!window.present) return "reset --";
  const uint64_t now = static_cast<uint64_t>(time(nullptr));
  uint64_t remaining = window.reset_at > now ? window.reset_at - now
                                              : window.reset_after_seconds;
  const uint32_t hours = remaining / 3600U;
  const uint32_t minutes = (remaining % 3600U) / 60U;
  char text[24];
  if (hours >= 24) {
    snprintf(text, sizeof(text), "reset %lud %02luh",
             static_cast<unsigned long>(hours / 24U),
             static_cast<unsigned long>(hours % 24U));
  } else {
    snprintf(text, sizeof(text), "reset %02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  }
  return text;
}

void CodexUsageApp::buildWindowRow(lv_obj_t* root, int16_t y, const char* label,
                                   const RateLimitWindow& window) {
  makeLabel(root, label, 5, y, &lv_font_montserrat_16);
  const uint8_t remaining = window.present ? window.remainingPercent() : 0;

  lv_obj_t* bar = lv_bar_create(root);
  lv_obj_set_pos(bar, 37, y + 2);
  lv_obj_set_size(bar, 135, 13);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, remaining, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);

  char percent[8];
  if (window.present) {
    snprintf(percent, sizeof(percent), "%u%%", remaining);
  } else {
    snprintf(percent, sizeof(percent), "--%%");
  }
  makeLabel(root, percent, 180, y - 2, &lv_font_montserrat_16);
  makeLabel(root, resetText(window).c_str(), 37, y + 15, &lv_font_montserrat_12);
}

void CodexUsageApp::buildUi(lv_obj_t* root, const AppState& app_state) {
  const auto& state = static_cast<const CodexAppState&>(app_state);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);

  makeLabel(root, "CODEX", 5, 1, &lv_font_montserrat_20);
  makeLabel(root, state.usage.plan_type.c_str(), 78, 5, &lv_font_montserrat_12);
  char battery[16];
  if (state.battery_mv >= 2500) {
    snprintf(battery, sizeof(battery), "BAT %.2fV", state.battery_mv / 1000.0F);
  } else {
    snprintf(battery, sizeof(battery), "USB");
  }
  lv_obj_t* battery_label = makeLabel(root, battery, 246, 5, &lv_font_montserrat_12);
  lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -4, 4);

  lv_obj_t* rule = lv_obj_create(root);
  lv_obj_set_pos(rule, 4, 23);
  lv_obj_set_size(rule, 242, 1);
  lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
  lv_obj_set_style_border_width(rule, 0, 0);

  buildWindowRow(root, 30, "5h", state.usage.five_hour);
  buildWindowRow(root, 67, "7d", state.usage.weekly);

  char footer[64];
  char last_update[24] = "last --:--";
  if (state.usage.synced_at > 0) {
    const time_t timestamp = static_cast<time_t>(state.usage.synced_at);
    struct tm local_time {};
    localtime_r(&timestamp, &local_time);
    strftime(last_update, sizeof(last_update), "last %m-%d %H:%M", &local_time);
  }
  const char* status = state.usage.status == SyncStatus::kAuthExpired
                           ? "REAUTHORIZE"
                           : syncStatusCode(state.usage.status);
  snprintf(footer, sizeof(footer), "%s  %s%s", status, last_update,
           state.usage.limit_reached ? "  EMPTY" : "");
  lv_obj_t* footer_label = makeLabel(root, footer, 4, 108, &lv_font_montserrat_12);
  lv_obj_set_width(footer_label, 242);
  lv_label_set_long_mode(footer_label, LV_LABEL_LONG_CLIP);
}

uint32_t CodexUsageApp::nextWakeSeconds(const UpdateResult& result) const {
  if (result.status == SyncStatus::kAuthExpired) return 6U * 3600U;
  return result.next_wake_sec;
}

}  // namespace epd
