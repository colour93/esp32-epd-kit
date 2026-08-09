#pragma once

#include <Arduino.h>

#include "toolkit/core_logic.h"

namespace epd {

enum class SyncStatus : uint8_t {
  kNever,
  kOk,
  kOffline,
  kAuthExpired,
  kForbidden,
  kThrottled,
  kProxyError,
  kTlsError,
  kTimeError,
  kProtocolError,
  kLowBattery,
};

struct RateLimitWindow {
  bool present = false;
  core::WindowKind kind = core::WindowKind::kUnknown;
  float used_percent = 0;
  uint32_t limit_window_seconds = 0;
  uint32_t reset_after_seconds = 0;
  uint64_t reset_at = 0;

  uint8_t remainingPercent() const {
    return core::remainingPercent(used_percent);
  }
};

struct AdditionalRateLimit {
  bool present = false;
  String name;
  String metered_feature;
  RateLimitWindow primary;
  RateLimitWindow secondary;
};

struct CodexUsageState {
  bool has_data = false;
  bool allowed = true;
  bool limit_reached = false;
  String plan_type = "--";
  SyncStatus status = SyncStatus::kNever;
  String status_detail;
  uint64_t synced_at = 0;
  RateLimitWindow five_hour;
  RateLimitWindow weekly;
  RateLimitWindow unknown;
  AdditionalRateLimit additional[2];
};

const char* syncStatusCode(SyncStatus status);

}  // namespace epd
