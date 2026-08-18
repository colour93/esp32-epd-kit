#pragma once

#include "toolkit/app.h"

namespace epd {

class HomePage : public IPage {
 public:
  const PageManifest& manifest() const override;
  void buildUi(lv_obj_t* root, const PageContext& context) override;
  void buildTimedRegion(const char* id, lv_obj_t* root,
                        const RuntimeContext& context) override;
  static void buildClock(lv_obj_t* parent, const Rect& bounds,
                         const RuntimeContext& context);
};

class HomeThreePage : public IPage {
 public:
  const PageManifest& manifest() const override;
  void buildUi(lv_obj_t* root, const PageContext& context) override;
  void buildTimedRegion(const char* id, lv_obj_t* root,
                        const RuntimeContext& context) override;
};

#if defined(EPD_PANEL_420)
class HomeSixPage : public IPage {
 public:
  const PageManifest& manifest() const override;
  void buildUi(lv_obj_t* root, const PageContext& context) override;
  void buildTimedRegion(const char* id, lv_obj_t* root,
                        const RuntimeContext& context) override;
};
#endif

}  // namespace epd
