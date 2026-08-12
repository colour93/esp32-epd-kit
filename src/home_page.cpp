#include "toolkit/home_page.h"

#include <time.h>

#include "toolkit/codex_usage_app.h"
#include "toolkit/feishu_project_app.h"
#include "toolkit/ui_fonts.h"

namespace epd {
namespace {

const PageWidget kLeftWidgets[] = {
    {"codex.usage.compact", "Codex 额度", "codex.rate_limits", 1},
    {"feishu.project.compact", "飞书项目", "feishu.project_card", 1},
};
const PageWidget kRightWidgets[] = {
    {"feishu.project.compact", "飞书项目", "feishu.project_card", 1},
    {"codex.usage.compact", "Codex 额度", "codex.rate_limits", 1},
};
const PageSlot kSlots[] = {
    {"codex", "左侧组件位", kLeftWidgets, 2, false, SlotStatus::kActive},
    {"feishu_project", "右侧组件位", kRightWidgets, 2, false,
     SlotStatus::kActive},
};
const TimedRegion kRegions[] = {
    {"clock", {8, 0, 234, 31}, 60},
};
const PageManifest kManifest{"home", "Home", kSlots, 2, kRegions, 1};

lv_obj_t* label(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                const lv_font_t* font) {
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

void verticalRule(lv_obj_t* parent, int16_t x, int16_t y, int16_t height) {
  lv_obj_t* object = lv_obj_create(parent);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, 1, height);
  lv_obj_set_style_bg_color(object, lv_color_black(), 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
}

void connectionIndicator(lv_obj_t* parent, bool connected) {
  lv_obj_t* indicator = lv_obj_create(parent);
  lv_obj_set_pos(indicator, 170, 9);
  lv_obj_set_size(indicator, 7, 7);
  lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(indicator, lv_color_black(), 0);
  lv_obj_set_style_border_width(indicator, 1, 0);
  lv_obj_set_style_bg_color(indicator, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(indicator, connected ? LV_OPA_COVER : LV_OPA_TRANSP,
                          0);
  lv_obj_set_style_pad_all(indicator, 0, 0);
  lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);

  label(parent, connected ? "已连接" : "离线", 182, 5, &ui_font_zh_14);
}

void buildWidget(lv_obj_t* parent, const Rect& bounds,
                 const SlotResource& slot, const RuntimeContext& runtime) {
  if (slot.widget == nullptr) return;
  const String widget_id(slot.widget->id);
  if (widget_id == "codex.usage.compact") {
    CodexUsageCompactWidget::build(
        parent, bounds, CodexUsageModel::fromSlot(slot, runtime));
  } else if (widget_id == "feishu.project.compact") {
    FeishuProjectCompactWidget::build(parent, bounds,
                                      FeishuProjectModel::fromSlot(slot));
  }
}

}  // namespace

const PageManifest& HomePage::manifest() const { return kManifest; }

void HomePage::buildClock(lv_obj_t* parent, const Rect& bounds,
                          const RuntimeContext& context) {
  lv_obj_t* root = lv_obj_create(parent);
  lv_obj_set_pos(root, bounds.x, bounds.y);
  lv_obj_set_size(root, bounds.width, bounds.height);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  char clock[12] = "--:--";
  char date[12] = "--/--";
  if (context.now >= 1700000000ULL) {
    const time_t timestamp = static_cast<time_t>(
        context.now + static_cast<int64_t>(context.utc_offset_minutes) * 60);
    struct tm local_time {};
    gmtime_r(&timestamp, &local_time);
    strftime(clock, sizeof(clock), "%H:%M", &local_time);
    strftime(date, sizeof(date), "%m/%d", &local_time);
  }
  label(root, clock, 0, -5, &lv_font_montserrat_28);
  lv_obj_t* date_label = label(root, date, 0, 6, &lv_font_montserrat_14);
  lv_obj_align(date_label, LV_ALIGN_TOP_MID, -4, 6);
  connectionIndicator(root, context.connected);
  rule(root, 0, bounds.height - 1, bounds.width);
}

void HomePage::buildUi(lv_obj_t* root, const PageContext& context) {
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  buildClock(root, kRegions[0].bounds, context.runtime);
  verticalRule(root, 125, 42, 72);
  buildWidget(root, {8, 38, 109, 82}, context.resources.get("codex"),
              context.runtime);
  buildWidget(root, {133, 38, 109, 82},
              context.resources.get("feishu_project"), context.runtime);
}

void HomePage::buildTimedRegion(const char* id, lv_obj_t* root,
                                const RuntimeContext& context) {
  if (String(id) != "clock") return;
  buildClock(root, {0, 0, kRegions[0].bounds.width, kRegions[0].bounds.height},
             context);
}

}  // namespace epd
