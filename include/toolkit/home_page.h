#pragma once

#include "toolkit/app.h"

namespace epd {

class HomePage : public IPage {
 public:
  const PageManifest& manifest() const override;
  void buildUi(lv_obj_t* root, const PageContext& context) override;
  void buildTimedRegion(const char* id, lv_obj_t* root,
                        const RuntimeContext& context) override;

 private:
  static void buildClock(lv_obj_t* parent, const Rect& bounds,
                         const RuntimeContext& context);
};

}  // namespace epd
