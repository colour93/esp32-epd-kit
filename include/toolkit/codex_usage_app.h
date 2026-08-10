#pragma once

#include "toolkit/app.h"
#include "toolkit/usage_model.h"

namespace epd {

class CodexUsageRenderer : public IRenderer {
 public:
  const char* id() const override { return "codex.rate_limits"; }
  const char* schemaId() const override { return "codex.rate_limits"; }
  uint16_t schemaVersion() const override { return 1; }
  void buildUi(lv_obj_t* root, const ResourceRecord* resource,
               const RenderContext& context) override;

 private:
  static CodexUsageState parse(const ResourceRecord* resource, uint64_t now);
  static String resetText(const RateLimitWindow& window, uint64_t now);
};

}  // namespace epd
