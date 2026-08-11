#include "toolkit/home_page.h"

#include <time.h>

#include "toolkit/codex_usage_app.h"
#include "toolkit/feishu_project_app.h"
#include "toolkit/ui_fonts.h"

namespace epd {
namespace {

const PageSlot kSlots[] = {
    {"codex", "codex.rate_limits", 1, true, SlotStatus::kActive},
    {"feishu_project", "feishu.project_card", 1, false, SlotStatus::kActive},
};
const TimedRegion kRegions[] = {
    {"clock", {8, 0, 234, 30}, 60},
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
  char date[20] = "时间未同步";
  if (context.now >= 1700000000ULL) {
    const time_t timestamp = static_cast<time_t>(
        context.now + static_cast<int64_t>(context.utc_offset_minutes) * 60);
    struct tm local_time {};
    gmtime_r(&timestamp, &local_time);
    strftime(clock, sizeof(clock), "%H:%M", &local_time);
    strftime(date, sizeof(date), "%m-%d", &local_time);
  }
  label(root, clock, 0, -5, &lv_font_montserrat_28);
  lv_obj_t* date_label = label(root, date, 0, 5,
                               context.now >= 1700000000ULL
                                   ? &lv_font_montserrat_14
                                   : &ui_font_zh_14);
  lv_obj_align(date_label, LV_ALIGN_TOP_RIGHT, 0, 5);
  rule(root, 0, bounds.height - 1, bounds.width);
}

void HomePage::buildUi(lv_obj_t* root, const PageContext& context) {
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  buildClock(root, kRegions[0].bounds, context.runtime);
  CodexUsageCompactWidget::build(
      root, {8, 34, 234, 49},
      CodexUsageModel::fromSlot(context.resources.get("codex"), context.runtime));
  FeishuProjectCompactWidget::build(
      root, {8, 86, 234, 34},
      FeishuProjectModel::fromSlot(context.resources.get("feishu_project")));
}

void HomePage::buildTimedRegion(const char* id, lv_obj_t* root,
                                const RuntimeContext& context) {
  if (String(id) != "clock") return;
  buildClock(root, {0, 0, kRegions[0].bounds.width, kRegions[0].bounds.height},
             context);
}

}  // namespace epd
