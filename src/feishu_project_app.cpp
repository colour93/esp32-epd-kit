#include "toolkit/feishu_project_app.h"

#include "toolkit/ui_fonts.h"

namespace epd {
namespace {

lv_obj_t* label(lv_obj_t* parent, const String& text, int16_t x, int16_t y,
                int16_t width, lv_text_align_t align) {
  lv_obj_t* object = lv_label_create(parent);
  lv_label_set_text(object, text.c_str());
  lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_width(object, width);
  lv_obj_set_style_text_align(object, align, 0);
  lv_obj_set_style_text_color(object, lv_color_black(), 0);
  lv_obj_set_style_text_font(object, &ui_font_zh_14, 0);
  return object;
}

const char* statusText(FeishuProjectStatus status) {
  switch (status) {
    case FeishuProjectStatus::kWaiting: return "等待数据";
    case FeishuProjectStatus::kFresh: return "";
    case FeishuProjectStatus::kStale: return "数据已过期";
    case FeishuProjectStatus::kUnconfigured: return "未配置";
    case FeishuProjectStatus::kDisabled: return "已停用";
    case FeishuProjectStatus::kInvalid: return "数据异常";
  }
  return "数据异常";
}

}  // namespace

FeishuProjectModel FeishuProjectModel::fromSlot(const SlotResource& slot) {
  FeishuProjectModel model;
  if (slot.state == ResourceState::kMissing || slot.resource == nullptr) {
    return model;
  }
  if (slot.state == ResourceState::kInvalid) {
    model.status = FeishuProjectStatus::kInvalid;
    return model;
  }
  JsonVariantConst payload = slot.resource->payload.as<JsonVariantConst>();
  if (!payload.is<JsonObjectConst>() ||
      !payload["source_status"].is<const char*>() ||
      !payload["display_name"].is<const char*>()) {
    model.status = FeishuProjectStatus::kInvalid;
    return model;
  }
  model.display_name = payload["display_name"].as<const char*>();
  const String source_status = payload["source_status"].as<const char*>();
  if (source_status == "unconfigured") {
    model.status = FeishuProjectStatus::kUnconfigured;
    return model;
  }
  if (source_status == "disabled") {
    model.status = FeishuProjectStatus::kDisabled;
    return model;
  }
  if (source_status != "ok" || !payload["value"].is<const char*>()) {
    model.status = FeishuProjectStatus::kInvalid;
    return model;
  }
  model.status = slot.state == ResourceState::kStale
                     ? FeishuProjectStatus::kStale
                     : FeishuProjectStatus::kFresh;
  model.value = payload["value"].as<const char*>();
  if (payload["detail"].is<const char*>()) {
    model.detail = payload["detail"].as<const char*>();
  }
  return model;
}

void FeishuProjectCompactWidget::build(lv_obj_t* parent, const Rect& bounds,
                                       const FeishuProjectModel& model) {
  lv_obj_t* root = lv_obj_create(parent);
  lv_obj_set_pos(root, bounds.x, bounds.y);
  lv_obj_set_size(root, bounds.width, bounds.height);
  lv_obj_set_style_bg_color(root, lv_color_white(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  label(root, model.display_name, 0, 0, bounds.width - 76,
        LV_TEXT_ALIGN_LEFT);
  label(root, model.value, bounds.width - 72, 0, 72, LV_TEXT_ALIGN_RIGHT);
  const String secondary = model.status == FeishuProjectStatus::kFresh
                               ? model.detail
                               : String(statusText(model.status));
  label(root, secondary, 0, 17, bounds.width, LV_TEXT_ALIGN_LEFT);

  lv_obj_t* rule = lv_obj_create(root);
  lv_obj_set_pos(rule, 0, bounds.height - 1);
  lv_obj_set_size(rule, bounds.width, 1);
  lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_pad_all(rule, 0, 0);
}

}  // namespace epd
