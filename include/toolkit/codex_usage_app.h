#pragma once

#include "toolkit/app.h"

namespace epd {

struct CodexAppState : public AppState {
  CodexUsageState usage;
  uint16_t battery_mv = 0;
};

class CodexUsageApp : public IApp {
 public:
  AppManifest manifest() const override;
  bool validateConfig(const DeviceConfig& config, String& error) const override;
  UpdateResult update(AppContext& context) override;
  void buildUi(lv_obj_t* root, const AppState& state) override;
  uint32_t nextWakeSeconds(const UpdateResult& result) const override;
  AppState& state() override { return state_; }

  CodexAppState& codexState() { return state_; }

 private:
  static void buildWindowRow(lv_obj_t* root, int16_t y, const char* label,
                             const RateLimitWindow& window);
  static String resetText(const RateLimitWindow& window);
  CodexAppState state_;
};

}  // namespace epd

