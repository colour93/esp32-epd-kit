#include "toolkit/serial_recovery.h"

#include <esp_system.h>
#include <stdlib.h>

namespace epd {
namespace {

bool deadlineReached(uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(millis() - deadline) >= 0;
}

bool parseCode(const String& value, uint32_t& code) {
  if (value.length() != 6) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed > UINT32_MAX) return false;
  code = static_cast<uint32_t>(parsed);
  return true;
}

}  // namespace

SerialRecoveryConsole::SerialRecoveryConsole(ConfigStore& config_store,
                                             BleProtocolService& ble)
    : config_store_(config_store), ble_(ble) {}

void SerialRecoveryConsole::begin() {
  Serial.begin(kBaudRate);
  delay(30);
  Serial.println();
  Serial.println("ESP32 E-Paper recovery console");
  Serial.println("Type 'help' for commands.");
  printPrompt();
}

void SerialRecoveryConsole::printPrompt() const { Serial.print("epd> "); }

void SerialRecoveryConsole::printHelp() const {
  Serial.println("Commands:");
  Serial.println("  status                         show recovery status");
  Serial.println("  setup                          allow a new bonded device for 120s");
  Serial.println("  io12 disable                   disable IO12 and restart");
  Serial.println("  restart                        restart the device");
  Serial.println("  factory-reset prepare          create a 30-second code");
  Serial.println("  factory-reset confirm <code>   erase all state and restart");
  Serial.println("  help                           show this help");
}

void SerialRecoveryConsole::printStatus() const {
  const DeviceConfig& config = ble_.config();
  Serial.printf("firmware=%s\n", EPD_TOOLKIT_VERSION);
  Serial.printf("uptime_ms=%lu\n", static_cast<unsigned long>(millis()));
  Serial.printf("ble_connected=%s\n", ble_.connected() ? "yes" : "no");
  Serial.printf("ble_authenticated=%s\n",
                ble_.authenticated() ? "yes" : "no");
  Serial.printf("owned=%s\n", ble_.owned() ? "yes" : "no");
  Serial.printf("setup=%s\n", ble_.setupMode() ? "active" : "closed");
  if (ble_.setupMode()) {
    Serial.printf("setup_remaining_sec=%lu\n",
                  static_cast<unsigned long>(ble_.setupRemainingSeconds()));
    Serial.printf("pairing_passkey=%06lu\n",
                  static_cast<unsigned long>(ble_.passkey()));
  }
  Serial.printf("config_revision=%lu\n",
                static_cast<unsigned long>(config.revision));
  Serial.printf("io12=%s\n",
                config.hardware.io12_mode == Io12Mode::kKey ? "key"
                                                            : "disabled");
  Serial.printf("power=%s\n",
                config.power.profile == PowerProfile::kBattery ? "battery"
                                                               : "mains");
  Serial.printf("free_heap=%lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

void SerialRecoveryConsole::enterSetup() {
  String error;
  if (!ble_.enterSetupMode(error)) {
    Serial.printf("[FAIL] cannot enter setup: %s\n", error.c_str());
    return;
  }
  Serial.println("[OK] setup active for 120 seconds");
  Serial.printf("[PASSKEY] %06lu\n",
                static_cast<unsigned long>(ble_.passkey()));
  Serial.println("[INFO] open EPD Agent and connect the new host");
}

void SerialRecoveryConsole::restart() const {
  Serial.println("[OK] restart scheduled");
  Serial.flush();
  delay(100);
  ESP.restart();
}

void SerialRecoveryConsole::disableIo12() {
  DeviceConfig config = ble_.config();
  if (config.hardware.io12_mode == Io12Mode::kDisabled) {
    Serial.println("[OK] IO12 is already disabled");
    return;
  }
  config.hardware.io12_mode = Io12Mode::kDisabled;
  ++config.revision;
  String error;
  if (!config_store_.save(config, error)) {
    Serial.printf("[FAIL] cannot disable IO12: %s\n", error.c_str());
    return;
  }
  Serial.println("[OK] IO12 disabled; restarting");
  Serial.flush();
  delay(100);
  ESP.restart();
}

void SerialRecoveryConsole::prepareFactoryReset() {
  factory_code_ = 100000U + esp_random() % 900000U;
  factory_deadline_ = millis() + kFactoryConfirmationMs;
  Serial.println("[WARN] this erases config, resources, owner, and BLE bonds");
  Serial.printf("[CONFIRM] run 'factory-reset confirm %06lu' within 30 seconds\n",
                static_cast<unsigned long>(factory_code_));
}

void SerialRecoveryConsole::confirmFactoryReset(const String& value) {
  uint32_t code = 0;
  if (!parseCode(value, code) || factory_deadline_ == 0 ||
      deadlineReached(factory_deadline_) || code != factory_code_) {
    Serial.println("[FAIL] confirmation code is invalid or expired");
    return;
  }
  factory_code_ = 0;
  factory_deadline_ = 0;
  String error;
  if (!ble_.factoryReset(error)) {
    Serial.printf("[FAIL] factory reset failed: %s\n", error.c_str());
    return;
  }
  Serial.println("[OK] factory state erased; restarting");
  Serial.flush();
}

void SerialRecoveryConsole::execute(String line) {
  line.trim();
  if (line.isEmpty()) return;

  const int first_space = line.indexOf(' ');
  String command = first_space < 0 ? line : line.substring(0, first_space);
  String arguments = first_space < 0 ? String() : line.substring(first_space + 1);
  command.toLowerCase();
  arguments.trim();

  String lower_arguments = arguments;
  lower_arguments.toLowerCase();
  if (command == "help" && arguments.isEmpty()) {
    printHelp();
  } else if (command == "status" && arguments.isEmpty()) {
    printStatus();
  } else if (command == "setup" && arguments.isEmpty()) {
    enterSetup();
  } else if (command == "restart" && arguments.isEmpty()) {
    restart();
  } else if (command == "io12" && lower_arguments == "disable") {
    disableIo12();
  } else if (command == "factory-reset" && lower_arguments == "prepare") {
    prepareFactoryReset();
  } else if (command == "factory-reset" &&
             lower_arguments.startsWith("confirm ")) {
    String value = arguments.substring(8);
    value.trim();
    confirmFactoryReset(value);
  } else {
    Serial.println("[FAIL] unknown command or wrong arguments; run: help");
  }
}

void SerialRecoveryConsole::loop() {
  if (deadlineReached(factory_deadline_)) {
    factory_code_ = 0;
    factory_deadline_ = 0;
  }
  while (Serial.available() > 0) {
    const char current = static_cast<char>(Serial.read());
    if (current == '\r') continue;
    if (current == '\b' || current == 0x7F) {
      if (!input_line_.isEmpty()) input_line_.remove(input_line_.length() - 1);
      continue;
    }
    if (current == '\n') {
      if (input_overflow_) {
        Serial.println("[FAIL] command is too long");
      } else {
        execute(input_line_);
      }
      input_line_ = "";
      input_overflow_ = false;
      printPrompt();
      continue;
    }
    if (!input_overflow_ && input_line_.length() < kMaxLineLength) {
      input_line_ += current;
    } else {
      input_overflow_ = true;
    }
  }
}

}  // namespace epd
