#pragma once

#include <Arduino.h>

#include "toolkit/config.h"

namespace epd {

class NetworkManager {
 public:
  bool connect(const WifiSettings& settings, uint32_t timeout_ms = 12000);
  bool syncClock(const DeviceSettings& settings, uint32_t timeout_ms = 10000);
  void disconnect();

  bool connected() const;
  static bool clockValid();
  const String& lastError() const { return last_error_; }

 private:
  String last_error_;
};

}  // namespace epd

