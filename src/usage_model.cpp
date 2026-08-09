#include "toolkit/usage_model.h"

namespace epd {

const char* syncStatusCode(SyncStatus status) {
  switch (status) {
    case SyncStatus::kNever:
      return "SETUP";
    case SyncStatus::kOk:
      return "OK";
    case SyncStatus::kOffline:
      return "OFFLINE";
    case SyncStatus::kAuthExpired:
      return "AUTH";
    case SyncStatus::kForbidden:
      return "DENIED";
    case SyncStatus::kThrottled:
      return "HTTP 429";
    case SyncStatus::kProxyError:
      return "PROXY";
    case SyncStatus::kTlsError:
      return "TLS";
    case SyncStatus::kTimeError:
      return "TIME";
    case SyncStatus::kProtocolError:
      return "DATA";
    case SyncStatus::kLowBattery:
      return "LOW BAT";
  }
  return "ERROR";
}

}  // namespace epd
