#include "toolkit/generic_metric_app.h"

#include <stdlib.h>

#include "toolkit/ui_fonts.h"

namespace epd {
namespace {

lv_obj_t* surface(lv_obj_t* parent, const Rect& bounds) {
  lv_obj_t* object = lv_obj_create(parent);
  lv_obj_set_pos(object, bounds.x, bounds.y);
  lv_obj_set_size(object, bounds.width, bounds.height);
  lv_obj_set_style_bg_color(object, lv_color_white(), 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  return object;
}

lv_obj_t* boundedLabel(lv_obj_t* parent, const String& text, int16_t x,
                       int16_t y, int16_t width, lv_text_align_t align,
                       const lv_font_t* font = &ui_font_zh_14) {
  lv_obj_t* object = lv_label_create(parent);
  lv_label_set_text(object, text.c_str());
  lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_width(object, width);
  lv_obj_set_style_text_align(object, align, 0);
  lv_obj_set_style_text_color(object, lv_color_black(), 0);
  lv_obj_set_style_text_font(object, font, 0);
  return object;
}

void titleBand(lv_obj_t* parent, const String& text, int16_t width) {
  lv_obj_t* background = lv_obj_create(parent);
  lv_obj_set_pos(background, 0, 0);
  lv_obj_set_size(background, width, 20);
  lv_obj_set_style_bg_color(background, lv_color_black(), 0);
  lv_obj_set_style_border_width(background, 0, 0);
  lv_obj_set_style_radius(background, 0, 0);
  lv_obj_set_style_pad_all(background, 0, 0);
  lv_obj_clear_flag(background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title = boundedLabel(background, text, 6, 1, width - 12,
                                 LV_TEXT_ALIGN_LEFT);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
}

const char* statusText(GenericMetricStatus status) {
  switch (status) {
    case GenericMetricStatus::kWaiting: return "等待数据";
    case GenericMetricStatus::kFresh: return "";
    case GenericMetricStatus::kStale: return "数据已过期";
    case GenericMetricStatus::kUnconfigured: return "未配置";
    case GenericMetricStatus::kDisabled: return "已停用";
    case GenericMetricStatus::kInvalid: return "数据异常";
  }
  return "数据异常";
}

String variantText(JsonVariantConst value) {
  if (value.is<const char*>()) return value.as<const char*>();
  if (value.is<bool>()) return value.as<bool>() ? "true" : "false";
  String encoded;
  serializeJson(value, encoded);
  return encoded;
}

String countdownText(const String& value, uint64_t now) {
  char* end = nullptr;
  const uint64_t timestamp = strtoull(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || timestamp == 0) return value;
  const uint64_t remaining = timestamp > now ? timestamp - now : 0;
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

String displayData(const GenericMetricItem& item, uint64_t now) {
  if (item.format == GenericMetricFormat::kCountdown) {
    return countdownText(item.data, now);
  }
  if (item.format == GenericMetricFormat::kPercent && item.data != "--") {
    return item.data + "%";
  }
  return item.data;
}

String secondaryText(const GenericMetricModel& model,
                     const GenericMetricItem* item) {
  if (model.status != GenericMetricStatus::kFresh) {
    return statusText(model.status);
  }
  return item == nullptr ? String("无此数据项") : item->description;
}

const char* iconSymbol(const String& icon) {
  if (icon == "sync") return LV_SYMBOL_REFRESH;
  if (icon == "check") return LV_SYMBOL_OK;
  if (icon == "close") return LV_SYMBOL_CLOSE;
  if (icon == "pause") return LV_SYMBOL_PAUSE;
  return "";
}

uint8_t itemProgress(const GenericMetricItem* item) {
  return item != nullptr && item->has_progress ? item->progress : 0;
}

void progressBar(lv_obj_t* parent, int16_t x, int16_t y, int16_t width,
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

}  // namespace

GenericMetricModel GenericMetricModel::fromSlot(
    const SlotResource& slot, const RuntimeContext& runtime) {
  GenericMetricModel model;
  model.now = runtime.now;
  if (slot.state == ResourceState::kMissing || slot.resource == nullptr) {
    return model;
  }
  if (slot.state == ResourceState::kInvalid) {
    model.status = GenericMetricStatus::kInvalid;
    return model;
  }
  JsonVariantConst payload = slot.resource->payload.as<JsonVariantConst>();
  if (!payload.is<JsonObjectConst>() || !payload["source_status"].is<const char*>() ||
      !payload["title"].is<const char*>()) {
    model.status = GenericMetricStatus::kInvalid;
    return model;
  }
  model.title = payload["title"].as<const char*>();
  const String source_status = payload["source_status"].as<const char*>();
  if (source_status == "unconfigured") {
    model.status = GenericMetricStatus::kUnconfigured;
    return model;
  }
  if (source_status == "disabled") {
    model.status = GenericMetricStatus::kDisabled;
    return model;
  }
  if (source_status != "ok" || !payload["items"].is<JsonArrayConst>()) {
    model.status = GenericMetricStatus::kInvalid;
    return model;
  }
  for (JsonVariantConst value : payload["items"].as<JsonArrayConst>()) {
    if (model.item_count >= kMaxItems || !value.is<JsonObjectConst>() ||
        !value["label"].is<const char*>() || value["data"].isNull()) {
      model.status = GenericMetricStatus::kInvalid;
      return model;
    }
    GenericMetricItem& item = model.items[model.item_count++];
    item.label = value["label"].as<const char*>();
    item.data = variantText(value["data"]);
    if (value["description"].is<const char*>()) {
      item.description = value["description"].as<const char*>();
    }
    if (value["icon"].is<const char*>()) {
      item.icon = value["icon"].as<const char*>();
    }
    const String format = value["format"] | "text";
    item.format = format == "percent"
                      ? GenericMetricFormat::kPercent
                      : format == "countdown" ? GenericMetricFormat::kCountdown
                                               : GenericMetricFormat::kText;
    if (value["progress"].is<double>() || value["progress"].is<long>()) {
      const int progress = value["progress"].as<int>();
      item.progress = static_cast<uint8_t>(progress < 0 ? 0 : progress > 100 ? 100 : progress);
      item.has_progress = true;
    }
  }
  model.status = slot.state == ResourceState::kStale
                     ? GenericMetricStatus::kStale
                     : GenericMetricStatus::kFresh;
  return model;
}

const GenericMetricItem* GenericMetricModel::item(size_t index) const {
  return index < item_count ? &items[index] : nullptr;
}

void GenericMetricDualWidget::build(lv_obj_t* parent, const Rect& bounds,
                                    const GenericMetricModel& model) {
  lv_obj_t* root = surface(parent, bounds);
  titleBand(root, model.title, bounds.width);
  const int16_t half = bounds.width / 2;
  for (size_t index = 0; index < 2; ++index) {
    const GenericMetricItem* item = model.item(index);
    const int16_t x = static_cast<int16_t>(index * half);
    boundedLabel(root, item == nullptr ? "--" : item->label, x + 4, 25,
                 half - 8, LV_TEXT_ALIGN_LEFT, &lv_font_montserrat_12);
    boundedLabel(root,
                 item == nullptr ? String("--") : displayData(*item, model.now),
                 x + 4, 42, half - 8, LV_TEXT_ALIGN_LEFT,
                 &lv_font_montserrat_20);
    if (index == 1) {
      lv_obj_t* rule = lv_obj_create(root);
      lv_obj_set_pos(rule, x, 27);
      lv_obj_set_size(rule, 1, bounds.height - 36);
      lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
      lv_obj_set_style_border_width(rule, 0, 0);
      lv_obj_set_style_pad_all(rule, 0, 0);
    }
  }
}

void GenericMetricValueWidget::build(lv_obj_t* parent, const Rect& bounds,
                                     const GenericMetricModel& model,
                                     size_t item_index) {
  lv_obj_t* root = surface(parent, bounds);
  titleBand(root, model.title, bounds.width);
  const GenericMetricItem* item = model.item(item_index);
  const bool has_icon = item != nullptr && !item->icon.isEmpty();
  if (has_icon) {
    boundedLabel(root, iconSymbol(item->icon), 4, 25, bounds.width - 8,
                 LV_TEXT_ALIGN_CENTER, &lv_font_montserrat_28);
    boundedLabel(root, displayData(*item, model.now), 4, 54,
                 bounds.width - 8, LV_TEXT_ALIGN_CENTER, &ui_font_zh_14);
    return;
  }
  const lv_font_t* data_font =
      item != nullptr && item->format == GenericMetricFormat::kText
          ? &ui_font_zh_16
          : bounds.width < 90 ? &ui_font_zh_16 : &lv_font_montserrat_20;
  boundedLabel(root, item == nullptr ? "--" : item->label, 4, 24,
               bounds.width - 8, LV_TEXT_ALIGN_CENTER);
  boundedLabel(root,
               item == nullptr ? String("--") : displayData(*item, model.now),
               4, 40, bounds.width - 8, LV_TEXT_ALIGN_CENTER, data_font);
  boundedLabel(root, secondaryText(model, item), 4, bounds.height - 18,
               bounds.width - 8, LV_TEXT_ALIGN_CENTER);
}

void GenericMetricBarWidget::build(lv_obj_t* parent, const Rect& bounds,
                                   const GenericMetricModel& model,
                                   size_t item_index) {
  lv_obj_t* root = surface(parent, bounds);
  titleBand(root, model.title, bounds.width);
  const GenericMetricItem* item = model.item(item_index);
  boundedLabel(root, item == nullptr ? "--" : item->label, 4, 24,
               bounds.width - 8, LV_TEXT_ALIGN_LEFT);
  boundedLabel(root,
               item == nullptr ? String("--") : displayData(*item, model.now),
               4, 39, bounds.width - 8, LV_TEXT_ALIGN_RIGHT,
               &lv_font_montserrat_16);
  progressBar(root, 5, bounds.height - 17, bounds.width - 10,
              itemProgress(item));
}

void GenericMetricRingWidget::build(lv_obj_t* parent, const Rect& bounds,
                                    const GenericMetricModel& model,
                                    size_t item_index) {
  lv_obj_t* root = surface(parent, bounds);
  titleBand(root, model.title, bounds.width);
  const GenericMetricItem* item = model.item(item_index);
  const int16_t available_diameter = bounds.width - 29;
  const int16_t diameter = available_diameter < 49 ? available_diameter : 49;
  lv_obj_t* arc = lv_arc_create(root);
  lv_obj_set_size(arc, diameter, diameter);
  lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 23);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_value(arc, itemProgress(item));
  lv_obj_set_style_arc_color(arc, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_black(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  boundedLabel(root,
               item == nullptr ? String("--") : displayData(*item, model.now),
               (bounds.width - diameter) / 2, 37, diameter,
               LV_TEXT_ALIGN_CENTER, &lv_font_montserrat_12);
  boundedLabel(root, item == nullptr ? "--" : item->label, 3,
               bounds.height - 15, bounds.width - 6, LV_TEXT_ALIGN_CENTER);
}

}  // namespace epd
