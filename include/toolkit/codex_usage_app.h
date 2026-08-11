#pragma once

#include "toolkit/app.h"
#include "toolkit/usage_model.h"

namespace epd {

struct CodexUsageModel {
  CodexUsageState usage;
  uint64_t now = 0;
  uint16_t battery_mv = 0;
  bool battery_enabled = false;
  bool connected = false;
  int16_t utc_offset_minutes = 0;

  static CodexUsageModel fromSlot(const SlotResource& slot,
                                  const RuntimeContext& runtime);
};

class CodexUsageFullWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const CodexUsageModel& model);
};

class CodexUsageCompactWidget {
 public:
  static void build(lv_obj_t* parent, const Rect& bounds,
                    const CodexUsageModel& model);
};

class CodexUsagePage : public IPage {
 public:
  const PageManifest& manifest() const override;
  void buildUi(lv_obj_t* root, const PageContext& context) override;
  void buildTimedRegion(const char*, lv_obj_t*,
                        const RuntimeContext&) override {}
};

}  // namespace epd
