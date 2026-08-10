#include "toolkit/usage_model.h"

namespace epd {

const char* syncStatusCode(SyncStatus status) {
  switch (status) {
    case SyncStatus::kWaiting: return "waiting";
    case SyncStatus::kFresh: return "fresh";
    case SyncStatus::kStale: return "stale";
    case SyncStatus::kAuthRequired: return "auth_required";
    case SyncStatus::kUnavailable: return "unavailable";
    case SyncStatus::kInvalid: return "invalid";
    case SyncStatus::kLowBattery: return "low_battery";
  }
  return "invalid";
}

}  // namespace epd
