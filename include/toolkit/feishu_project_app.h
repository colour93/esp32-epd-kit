#pragma once

#include "toolkit/app.h"

namespace epd {

enum class FeishuProjectStatus : uint8_t {
  kWaiting,
  kFresh,
  kStale,
  kUnconfigured,
  kDisabled,
  kInvalid,
};

struct FeishuProjectModel {
  FeishuProjectStatus status = FeishuProjectStatus::kWaiting;
  String display_name = "飞书项目";
  String value = "--";
  String detail;

  static FeishuProjectModel fromSlot(const SlotResource& slot);
};

class FeishuProjectCompactWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const FeishuProjectModel& model);
};

}  // namespace epd
