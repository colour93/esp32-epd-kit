#pragma once

#include <Arduino.h>

namespace epd {

enum class SyncStatus : uint8_t {
  kWaiting,
  kFresh,
  kStale,
  kAuthRequired,
  kUnavailable,
  kInvalid,
  kLowBattery,
};

struct RateLimitWindow {
  bool present = false;
  uint8_t used_percent = 0;
  uint32_t window_duration_mins = 0;
  uint64_t resets_at = 0;

  uint8_t remainingPercent() const {
    return used_percent >= 100 ? 0 : static_cast<uint8_t>(100 - used_percent);
  }
};

struct CodexUsageState {
  bool has_data = false;
  bool limit_reached = false;
  String plan_type = "--";
  String limit_name;
  SyncStatus status = SyncStatus::kWaiting;
  uint64_t updated_at = 0;
  RateLimitWindow primary;
  RateLimitWindow secondary;
};

const char* syncStatusCode(SyncStatus status);

}  // namespace epd
