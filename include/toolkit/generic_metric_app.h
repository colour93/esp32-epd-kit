#pragma once

#include "toolkit/app.h"

namespace epd {

enum class GenericMetricStatus : uint8_t {
  kWaiting,
  kFresh,
  kStale,
  kUnconfigured,
  kDisabled,
  kInvalid,
};

enum class GenericMetricFormat : uint8_t { kText, kPercent, kCountdown };

struct GenericMetricItem {
  String label;
  String data = "--";
  String description;
  String icon;
  GenericMetricFormat format = GenericMetricFormat::kText;
  uint8_t progress = 0;
  bool has_progress = false;
};

struct GenericMetricModel {
  static constexpr size_t kMaxItems = 4;

  GenericMetricStatus status = GenericMetricStatus::kWaiting;
  String title = "数据";
  GenericMetricItem items[kMaxItems];
  size_t item_count = 0;
  uint64_t now = 0;

  static GenericMetricModel fromSlot(const SlotResource& slot,
                                     const RuntimeContext& runtime);
  const GenericMetricItem* item(size_t index) const;
};

class GenericMetricDualWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const GenericMetricModel& model);
};

class GenericMetricValueWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const GenericMetricModel& model, size_t item_index);
};

class GenericMetricBarWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const GenericMetricModel& model, size_t item_index);
};

class GenericMetricRingWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const GenericMetricModel& model, size_t item_index);
};

}  // namespace epd
