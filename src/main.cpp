#include <Arduino.h>

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>

#include "toolkit/app.h"
#include "toolkit/ble_provisioning.h"
#include "toolkit/codex_usage_app.h"
#include "toolkit/config_store.h"
#include "toolkit/display_manager.h"
#include "toolkit/hardware.h"
#include "toolkit/log.h"
#include "toolkit/resource_store.h"
#include "toolkit/serial_recovery.h"

namespace {

constexpr uint64_t kCriticalSleepMicros = 30ULL * 60ULL * 1000000ULL;
constexpr uint32_t kBatterySampleIntervalMs = 60000U;

epd::ConfigStore g_config_store;
epd::ResourceStore g_resources;
epd::CodexUsageRenderer g_codex_renderer;
epd::RendererRegistry g_renderers(g_codex_renderer);
epd::DisplayManager g_display;
epd::BleProtocolService g_ble(g_config_store, g_resources, g_renderers,
                              g_display);
epd::SerialRecoveryConsole g_serial(g_config_store, g_ble);

uint16_t g_battery_mv = 0;
uint32_t g_last_battery_sample = 0;
bool g_key_pressed = false;
uint32_t g_key_changed_at = 0;
uint32_t g_last_storage_error_at = 0;

uint16_t readBatteryMillivolts(const epd::DeviceConfig& config) {
  if (!config.hardware.battery.enabled) return 0;
  pinMode(epd::hardware::kBatteryAdc, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(epd::hardware::kBatteryAdc, ADC_11db);
  delay(5);
  analogReadMilliVolts(epd::hardware::kBatteryAdc);
  uint16_t samples[9]{};
  for (uint8_t index = 0; index < 9; ++index) {
    samples[index] = analogReadMilliVolts(epd::hardware::kBatteryAdc);
    delay(2);
  }
  std::sort(std::begin(samples), std::end(samples));
  const uint32_t millivolts = static_cast<uint32_t>(samples[4]) * 3U;
  return millivolts >= 2500U && millivolts <= 5000U
             ? static_cast<uint16_t>(millivolts)
             : 0;
}

void initializeFreshSecurity() {
  Preferences security;
  if (security.begin("epd_sec3", false)) {
    security.clear();
    security.end();
  }
  NimBLEDevice::init("EPD-KIT-RESET");
  NimBLEDevice::deleteAllBonds();
  NimBLEDevice::deinit(true);
}

epd::RenderContext renderContext() {
  epd::RenderContext context;
  context.now = static_cast<uint64_t>(time(nullptr));
  context.battery_mv = g_battery_mv;
  context.battery_enabled = g_ble.config().hardware.battery.enabled;
  context.connected = g_ble.sessionReady();
  context.utc_offset_minutes = g_ble.utcOffsetMinutes();
  return context;
}

epd::PresentResult renderCurrent(bool force_full) {
  const epd::DeviceConfig& config = g_ble.config();
  epd::IRenderer* renderer = g_renderers.find(config.view.renderer_id);
  if (renderer == nullptr) renderer = &g_codex_renderer;
  const epd::ResourceRecord* resource =
      g_resources.get(config.view.resource_key);
  TOOLKIT_LOG("render", String("build renderer=") + renderer->id() +
                            " resource=" + config.view.resource_key +
                            " force_full=" + (force_full ? "yes" : "no"));
  g_display.renderView(*renderer, resource, renderContext());
  return g_display.present(config.display, force_full);
}

[[noreturn]] void enterCriticalSleep(const epd::DeviceConfig& config) {
  TOOLKIT_LOG("power", String("critical battery ") + g_battery_mv +
                           "mV; BLE and display suspended");
  g_ble.stop();
  if (!g_display.lowBatteryLatched()) {
    g_display.renderLowBattery(g_battery_mv);
    g_display.present(config.display, true);
    g_display.setLowBatteryLatched(true);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(kCriticalSleepMicros);
#ifndef EPD_TOOLKIT_RELEASE
  Serial.flush();
#endif
  esp_deep_sleep_start();
  abort();
}

[[noreturn]] void enterScheduledSleep(const epd::DeviceConfig& config) {
  TOOLKIT_LOG("power", String("sync window complete; sleeping ") +
                           config.power.wake_interval_sec + "s");
  renderCurrent(false);
  g_ble.stop();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(
      static_cast<uint64_t>(config.power.wake_interval_sec) * 1000000ULL);
  if (config.hardware.io12_mode == epd::Io12Mode::kKey) {
    const uint32_t release_deadline = millis() + 2000U;
    while (digitalRead(epd::hardware::kUserKey) == LOW &&
           static_cast<int32_t>(millis() - release_deadline) < 0) {
      delay(10);
    }
    rtc_gpio_pullup_dis(
        static_cast<gpio_num_t>(epd::hardware::kUserKey));
    rtc_gpio_pulldown_dis(
        static_cast<gpio_num_t>(epd::hardware::kUserKey));
    esp_sleep_enable_ext0_wakeup(
        static_cast<gpio_num_t>(epd::hardware::kUserKey), 0);
  }
#ifndef EPD_TOOLKIT_RELEASE
  Serial.flush();
#endif
  esp_deep_sleep_start();
  abort();
}

void handleKey() {
  if (g_ble.config().hardware.io12_mode != epd::Io12Mode::kKey) return;
  const bool pressed = digitalRead(epd::hardware::kUserKey) == LOW;
  if (pressed == g_key_pressed) return;
  if (millis() - g_key_changed_at < 35U) return;
  g_key_changed_at = millis();
  g_key_pressed = pressed;
  if (!pressed) {
    TOOLKIT_LOG("input", "IO12 short press");
    g_ble.emitKeyPressed();
  }
}

}  // namespace

void setup() {
  g_serial.begin();
  TOOLKIT_LOG("core", String("boot firmware=") + EPD_TOOLKIT_VERSION);
  epd::DeviceConfig config;
  const bool loaded = g_config_store.load(config);
  if (!loaded) {
    config = epd::DeviceConfig{};
    config.revision = 1;
    String save_error;
    if (!g_config_store.save(config, save_error)) {
      TOOLKIT_LOG("config", String("default save failed: ") + save_error);
    }
    initializeFreshSecurity();
    TOOLKIT_LOG("security", "fresh security state initialized");
  }
  String resource_error;
  if (!g_resources.load(resource_error)) {
    TOOLKIT_LOG("resource", String("load failed: ") + resource_error);
  }
  TOOLKIT_LOG("core", String("config revision=") + config.revision +
                          " battery=" +
                          (config.hardware.battery.enabled ? "enabled" : "disabled") +
                          " io12=" +
                          (config.hardware.io12_mode == epd::Io12Mode::kKey
                               ? "key"
                               : "disabled") +
                          " power=" +
                          (config.power.profile == epd::PowerProfile::kMains
                               ? "mains"
                               : "battery"));

  if (config.hardware.io12_mode == epd::Io12Mode::kKey) {
    pinMode(epd::hardware::kUserKey, INPUT_PULLUP);
    TOOLKIT_LOG("input", "IO12 key input enabled");
  } else {
    TOOLKIT_LOG("input", "IO12 left high impedance");
  }

  g_battery_mv = readBatteryMillivolts(config);
  if (config.hardware.battery.enabled) {
    TOOLKIT_LOG("power", String("battery sample=") + g_battery_mv + "mV");
  } else {
    TOOLKIT_LOG("power", "battery ADC disabled");
  }
  g_display.begin();
  if (!config.hardware.battery.enabled) {
    g_display.setLowBatteryLatched(false);
  } else if (g_battery_mv >= config.hardware.battery.recovery_mv) {
    g_display.setLowBatteryLatched(false);
  } else if ((g_battery_mv > 0 &&
              g_battery_mv <= config.hardware.battery.critical_mv) ||
             (g_display.lowBatteryLatched() &&
              g_battery_mv < config.hardware.battery.recovery_mv)) {
    enterCriticalSleep(config);
  }
  const uint32_t passkey = 100000U + esp_random() % 900000U;
  if (!g_ble.begin(config, g_battery_mv, passkey)) {
    TOOLKIT_LOG("ble", "BLE v3 initialization failed");
  } else {
    TOOLKIT_LOG("ble", "BLE v3 service ready");
  }
  const bool deep_sleep_wake =
      esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  if (g_ble.owned() && !deep_sleep_wake) {
    const epd::PresentResult result = renderCurrent(true);
    TOOLKIT_LOG("display", String("initial refresh result=") +
                               (result == epd::PresentResult::kFull
                                    ? "full"
                                    : result == epd::PresentResult::kPartial
                                          ? "partial"
                                          : "unchanged"));
  }
  g_last_battery_sample = millis();
}

void loop() {
  g_serial.loop();
  g_ble.loop();
  handleKey();

  uint32_t factory_code = 0;
  if (g_ble.takeFactoryCode(factory_code)) {
    TOOLKIT_LOG("security", "showing factory reset confirmation");
    g_display.renderFactoryResetConfirmation(factory_code);
    g_display.present(g_ble.config().display);
  }

  bool force_full = false;
  if (g_ble.takeRenderRequest(force_full)) {
    TOOLKIT_LOG("display", String("refresh started mode=") +
                               (force_full ? "full" : "auto"));
    g_ble.emitDisplayStarted(force_full);
    const epd::PresentResult result = renderCurrent(force_full);
    g_ble.emitDisplayCompleted(
        result == epd::PresentResult::kFull
            ? "full"
            : result == epd::PresentResult::kPartial ? "partial" : "unchanged");
    TOOLKIT_LOG("display", String("refresh completed result=") +
                               (result == epd::PresentResult::kFull
                                    ? "full"
                                    : result == epd::PresentResult::kPartial
                                          ? "partial"
                                          : "unchanged"));
  }

  if (g_ble.config().hardware.battery.enabled &&
      millis() - g_last_battery_sample >= kBatterySampleIntervalMs) {
    g_last_battery_sample = millis();
    g_battery_mv = readBatteryMillivolts(g_ble.config());
    TOOLKIT_LOG("power", String("battery sample=") + g_battery_mv + "mV");
    g_ble.updateBattery(g_battery_mv);
    if (g_battery_mv > 0 &&
        g_battery_mv <= g_ble.config().hardware.battery.critical_mv) {
      enterCriticalSleep(g_ble.config());
    }
  }

  String save_error;
  if (!g_resources.saveIfDue(static_cast<uint64_t>(time(nullptr)), false,
                             save_error) &&
      millis() - g_last_storage_error_at >= 5000U) {
    g_last_storage_error_at = millis();
    TOOLKIT_LOG("resource", String("snapshot save failed: ") + save_error);
  }
  const bool factory_reset = g_ble.takeFactoryResetRequest();
  const bool restart = g_ble.takeRestartRequest();
  if (factory_reset || restart) {
    TOOLKIT_LOG("core", factory_reset ? "factory reset restart" : "restart requested");
    delay(250);
    ESP.restart();
  }
  if (g_ble.takeSleepRequest()) {
    enterScheduledSleep(g_ble.config());
  }
  delay(10);
}
