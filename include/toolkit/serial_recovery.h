#pragma once

#include <Arduino.h>

#include "toolkit/ble_provisioning.h"
#include "toolkit/config_store.h"
#include "toolkit/lan_transport.h"

namespace epd {

class SerialRecoveryConsole {
 public:
  SerialRecoveryConsole(ConfigStore& config_store,
                        BleProtocolService& ble, LanTransport& lan);

  void begin();
  void loop();

 private:
  static constexpr uint32_t kBaudRate = 115200;
  static constexpr uint32_t kFactoryConfirmationMs = 30000;
  static constexpr size_t kMaxLineLength = 192;

  void execute(String line);
  void printHelp() const;
  void printStatus() const;
  void disableIo12();
  void enterSetup();
  void prepareFactoryReset();
  void confirmFactoryReset(const String& value);
  void restart() const;
  void executeWifi(const String& arguments);
  void printPrompt() const;

  ConfigStore& config_store_;
  BleProtocolService& ble_;
  LanTransport& lan_;
  String input_line_;
  uint32_t factory_code_ = 0;
  uint32_t factory_deadline_ = 0;
  bool input_overflow_ = false;
};

}  // namespace epd
