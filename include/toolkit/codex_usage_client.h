#pragma once

#include "toolkit/config.h"
#include "toolkit/usage_model.h"

namespace epd {

class CodexUsageClient {
 public:
  SyncStatus fetch(const CodexSettings& settings, const String& locale,
                   CodexUsageState& state);

  static constexpr const char* kEndpoint =
      "https://chatgpt.com/backend-api/wham/usage";

 private:
  static constexpr size_t kMaxResponseBytes = 16U * 1024U;
  static RateLimitWindow parseWindow(JsonVariantConst value);
  static void assignWindow(const RateLimitWindow& window, CodexUsageState& state);
};

}  // namespace epd

