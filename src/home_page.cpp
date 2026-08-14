#include "toolkit/home_page.h"

#include <time.h>

#include "toolkit/codex_usage_app.h"
#include "toolkit/generic_metric_app.h"
#include "toolkit/hardware.h"
#include "toolkit/ui_fonts.h"

namespace epd {
namespace {

const PageWidget kTwoColumnWidgets[] = {
    {"codex.usage.compact", "Codex 双窗口", "codex.rate_limits", 1},
    {"generic.metric.dual", "双数据", "generic.metrics", 1},
    {"generic.metric.value.1", "数值 · 数据 1", "generic.metrics", 1},
    {"generic.metric.value.2", "数值 · 数据 2", "generic.metrics", 1},
    {"generic.metric.value.3", "数值 · 数据 3", "generic.metrics", 1},
    {"generic.metric.value.4", "数值 · 数据 4", "generic.metrics", 1},
    {"generic.metric.bar.1", "条形进度 · 数据 1", "generic.metrics", 1},
    {"generic.metric.bar.2", "条形进度 · 数据 2", "generic.metrics", 1},
    {"generic.metric.bar.3", "条形进度 · 数据 3", "generic.metrics", 1},
    {"generic.metric.bar.4", "条形进度 · 数据 4", "generic.metrics", 1},
    {"generic.metric.ring.1", "环形进度 · 数据 1", "generic.metrics", 1},
    {"generic.metric.ring.2", "环形进度 · 数据 2", "generic.metrics", 1},
    {"generic.metric.ring.3", "环形进度 · 数据 3", "generic.metrics", 1},
    {"generic.metric.ring.4", "环形进度 · 数据 4", "generic.metrics", 1},
};
const PageWidget kSingleWidgets[] = {
    {"generic.metric.value.1", "数值 · 数据 1", "generic.metrics", 1},
    {"generic.metric.value.2", "数值 · 数据 2", "generic.metrics", 1},
    {"generic.metric.value.3", "数值 · 数据 3", "generic.metrics", 1},
    {"generic.metric.value.4", "数值 · 数据 4", "generic.metrics", 1},
    {"generic.metric.bar.1", "条形进度 · 数据 1", "generic.metrics", 1},
    {"generic.metric.bar.2", "条形进度 · 数据 2", "generic.metrics", 1},
    {"generic.metric.bar.3", "条形进度 · 数据 3", "generic.metrics", 1},
    {"generic.metric.bar.4", "条形进度 · 数据 4", "generic.metrics", 1},
    {"generic.metric.ring.1", "环形进度 · 数据 1", "generic.metrics", 1},
    {"generic.metric.ring.2", "环形进度 · 数据 2", "generic.metrics", 1},
    {"generic.metric.ring.3", "环形进度 · 数据 3", "generic.metrics", 1},
    {"generic.metric.ring.4", "环形进度 · 数据 4", "generic.metrics", 1},
};
const PageSlot kSlots[] = {
    {"primary", "组件 1", kTwoColumnWidgets,
     sizeof(kTwoColumnWidgets) / sizeof(kTwoColumnWidgets[0]), false,
     SlotStatus::kActive},
    {"secondary", "组件 2", kTwoColumnWidgets,
     sizeof(kTwoColumnWidgets) / sizeof(kTwoColumnWidgets[0]), false,
     SlotStatus::kActive},
};
const PageSlot kThreeSlots[] = {
    {"first", "组件 1", kSingleWidgets,
     sizeof(kSingleWidgets) / sizeof(kSingleWidgets[0]), false,
     SlotStatus::kActive},
    {"second", "组件 2", kSingleWidgets,
     sizeof(kSingleWidgets) / sizeof(kSingleWidgets[0]), false,
     SlotStatus::kActive},
    {"third", "组件 3", kSingleWidgets,
     sizeof(kSingleWidgets) / sizeof(kSingleWidgets[0]), false,
     SlotStatus::kActive},
};
constexpr int16_t kOuterMargin = 8;
constexpr int16_t kTwoColumnGap = 16;
constexpr int16_t kThreeColumnGap = 9;
constexpr int16_t kHeaderHeight =
    static_cast<int16_t>(hardware::kLogicalHeight * 31U / 122U);
constexpr int16_t kContentTop = kHeaderHeight + 7;
constexpr int16_t kDividerTop = kHeaderHeight + 11;
constexpr int16_t kContentHeight =
    hardware::kLogicalHeight - kContentTop - 2;
constexpr int16_t kDividerHeight =
    hardware::kLogicalHeight - kDividerTop - 8;

const TimedRegion kRegions[] = {{
    "clock",
    {kOuterMargin, 0,
     static_cast<int16_t>(hardware::kLogicalWidth - 2 * kOuterMargin),
     kHeaderHeight},
    60,
}};
const PageManifest kManifest{"home", "Home", kSlots, 2, kRegions, 1};
const PageManifest kThreeManifest{"home.three", "Home · 3 组件",
                                  kThreeSlots, 3, kRegions, 1};

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

void connectionIndicator(lv_obj_t* parent, int16_t width, bool connected) {
  lv_obj_t* indicator = lv_obj_create(parent);
  lv_obj_set_pos(indicator, width - 64, 9);
  lv_obj_set_size(indicator, 7, 7);
  lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(indicator, lv_color_black(), 0);
  lv_obj_set_style_border_width(indicator, 1, 0);
  lv_obj_set_style_bg_color(indicator, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(indicator, connected ? LV_OPA_COVER : LV_OPA_TRANSP,
                          0);
  lv_obj_set_style_pad_all(indicator, 0, 0);
  lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
  label(parent, connected ? "已连接" : "离线", width - 52, 5,
        &ui_font_zh_14);
}

void buildWidget(lv_obj_t* parent, const Rect& bounds,
                 const SlotResource& slot, const RuntimeContext& runtime) {
  if (slot.widget == nullptr) return;
  const String widget_id(slot.widget->id);
  if (widget_id == "codex.usage.compact") {
    CodexUsageCompactWidget::build(
        parent, bounds, CodexUsageModel::fromSlot(slot, runtime));
    return;
  }
  const GenericMetricModel model = GenericMetricModel::fromSlot(slot, runtime);
  if (widget_id == "generic.metric.dual") {
    GenericMetricDualWidget::build(parent, bounds, model);
    return;
  }
  const int item_index = widget_id.substring(widget_id.length() - 1).toInt() - 1;
  if (item_index < 0 || item_index >= 4) return;
  if (widget_id.startsWith("generic.metric.value.")) {
    GenericMetricValueWidget::build(parent, bounds, model, item_index);
  } else if (widget_id.startsWith("generic.metric.bar.")) {
    GenericMetricBarWidget::build(parent, bounds, model, item_index);
  } else if (widget_id.startsWith("generic.metric.ring.")) {
    GenericMetricRingWidget::build(parent, bounds, model, item_index);
  }
}

void initializeRoot(lv_obj_t* root) {
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
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
  connectionIndicator(root, bounds.width, context.connected);
  rule(root, 0, bounds.height - 1, bounds.width);
}

void HomePage::buildUi(lv_obj_t* root, const PageContext& context) {
  initializeRoot(root);
  buildClock(root, kRegions[0].bounds, context.runtime);
  const int16_t center = hardware::kLogicalWidth / 2;
  const int16_t half_gap = kTwoColumnGap / 2;
  verticalRule(root, center, kDividerTop, kDividerHeight);
  buildWidget(root,
              {kOuterMargin, kContentTop,
               static_cast<int16_t>(center - half_gap - kOuterMargin),
               kContentHeight},
              context.resources.get("primary"), context.runtime);
  const int16_t secondary_x = center + half_gap;
  buildWidget(root,
              {secondary_x, kContentTop,
               static_cast<int16_t>(hardware::kLogicalWidth - kOuterMargin -
                                    secondary_x),
               kContentHeight},
              context.resources.get("secondary"), context.runtime);
}

void HomePage::buildTimedRegion(const char* id, lv_obj_t* root,
                                const RuntimeContext& context) {
  if (String(id) != "clock") return;
  buildClock(root, {0, 0, kRegions[0].bounds.width, kRegions[0].bounds.height},
             context);
}

const PageManifest& HomeThreePage::manifest() const { return kThreeManifest; }

void HomeThreePage::buildUi(lv_obj_t* root, const PageContext& context) {
  initializeRoot(root);
  HomePage::buildClock(root, kRegions[0].bounds, context.runtime);
  const int16_t column_width =
      (hardware::kLogicalWidth - 2 * kOuterMargin -
       2 * kThreeColumnGap) /
      3;
  const int16_t second_x = kOuterMargin + column_width + kThreeColumnGap;
  const int16_t third_x = second_x + column_width + kThreeColumnGap;
  verticalRule(root, kOuterMargin + column_width + kThreeColumnGap / 2,
               kDividerTop, kDividerHeight);
  verticalRule(root, second_x + column_width + kThreeColumnGap / 2,
               kDividerTop, kDividerHeight);
  buildWidget(root,
              {kOuterMargin, kContentTop, column_width, kContentHeight},
              context.resources.get("first"),
              context.runtime);
  buildWidget(root, {second_x, kContentTop, column_width, kContentHeight},
              context.resources.get("second"),
              context.runtime);
  buildWidget(root,
              {third_x, kContentTop,
               static_cast<int16_t>(hardware::kLogicalWidth - kOuterMargin -
                                    third_x),
               kContentHeight},
              context.resources.get("third"),
              context.runtime);
}

void HomeThreePage::buildTimedRegion(const char* id, lv_obj_t* root,
                                     const RuntimeContext& context) {
  if (String(id) != "clock") return;
  HomePage::buildClock(
      root, {0, 0, kRegions[0].bounds.width, kRegions[0].bounds.height},
      context);
}

}  // namespace epd
