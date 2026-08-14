#include <Arduino.h>

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <driver/gpio.h>
#if CONFIG_IDF_TARGET_ESP32
#include <driver/rtc_io.h>
#endif
#include <esp_attr.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>

#include "toolkit/app.h"
#include "toolkit/ble_provisioning.h"
#include "toolkit/codex_usage_app.h"
#include "toolkit/config_store.h"
#include "toolkit/display_manager.h"
#include "toolkit/hardware.h"
#include "toolkit/home_page.h"
#include "toolkit/log.h"
#include "toolkit/resource_store.h"
#include "toolkit/serial_recovery.h"

namespace {

constexpr uint64_t kCriticalSleepMicros = 30ULL * 60ULL * 1000000ULL;
constexpr uint32_t kBatterySampleIntervalMs = 60000U;
constexpr uint32_t kRtcPowerMagic = 0x45504434U;  // "EPD4"
constexpr uint64_t kValidUnixTime = 1700000000ULL;

struct RtcPowerState {
  uint32_t magic = 0;
  uint32_t page_hash = 0;
  uint64_t next_sync_at = 0;
  uint64_t next_page_tick_at = 0;
  int16_t utc_offset_minutes = 0;
};

RTC_DATA_ATTR RtcPowerState g_rtc_power{};

epd::ConfigStore g_config_store;
epd::ResourceStore g_resources;
epd::CodexUsagePage g_codex_page;
epd::HomePage g_home_page;
epd::HomeThreePage g_home_three_page;
epd::PageRegistry g_pages;
epd::DisplayManager g_display;
epd::BleProtocolService g_ble(g_config_store, g_resources, g_pages, g_display);
epd::SerialRecoveryConsole g_serial(g_config_store, g_ble);

uint16_t g_battery_mv = 0;
uint32_t g_last_battery_sample = 0;
bool g_key_pressed = false;
uint32_t g_key_changed_at = 0;
uint32_t g_last_storage_error_at = 0;
uint64_t g_next_mains_page_tick = 0;

uint64_t unixNow() {
  const time_t value = time(nullptr);
  return value > 0 ? static_cast<uint64_t>(value) : 0;
}

uint64_t nextBoundary(uint64_t now, uint32_t interval_sec) {
  if (now < kValidUnixTime || interval_sec == 0) return 0;
  return (now / interval_sec + 1U) * interval_sec;
}

void registerPages() {
  String error;
  if (!g_pages.add(g_home_page, error) ||
      !g_pages.add(g_home_three_page, error) ||
      !g_pages.add(g_codex_page, error)) {
    TOOLKIT_LOG("page", String("registry error: ") + error);
  }
}

uint16_t readBatteryMillivolts(const epd::DeviceConfig& config) {
  if (!epd::hardware::kHasBatteryAdc || !config.hardware.battery.enabled) {
    return 0;
  }
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
  if (security.begin("epd_sec4", false)) {
    security.clear();
    security.end();
  }
  NimBLEDevice::init("EPD-KIT-RESET");
  NimBLEDevice::deleteAllBonds();
  NimBLEDevice::deinit(true);
}

epd::RuntimeContext runtimeContext(const epd::DeviceConfig& config,
                                   int16_t utc_offset_minutes,
                                   bool connected) {
  epd::RuntimeContext context;
  context.now = unixNow();
  context.battery_mv = g_battery_mv;
  context.battery_enabled =
      epd::hardware::kHasBatteryAdc && config.hardware.battery.enabled;
  context.connected = connected;
  context.utc_offset_minutes = utc_offset_minutes;
  return context;
}

epd::PresentResult renderCurrent(bool force_full) {
  const epd::DeviceConfig& config = g_ble.config();
  epd::IPage* page = g_pages.find(config.page.id);
  TOOLKIT_LOG("render", String("build page=") + config.page.id +
                            " force_full=" + (force_full ? "yes" : "no"));
  if (page == nullptr) {
    g_display.renderPageDiagnostic(config.page.id);
    return g_display.present(config.display, force_full);
  }
  const epd::RuntimeContext runtime =
      runtimeContext(config, g_ble.utcOffsetMinutes(), g_ble.sessionReady());
  const epd::PageResources resources(page->manifest(), config.page, g_resources,
                                     runtime.now);
  const epd::PageContext context{resources, runtime};
  g_display.renderPage(*page, context, epd::pageIdentityHash(config.page));
  return g_display.present(config.display, force_full);
}

bool renderTimedRegion(const epd::DeviceConfig& config,
                       int16_t utc_offset_minutes, bool connected) {
  epd::IPage* page = g_pages.find(config.page.id);
  if (page == nullptr || page->manifest().timed_region_count == 0) return false;
  const epd::TimedRegion& region = page->manifest().timed_regions[0];
  const epd::RuntimeContext runtime =
      runtimeContext(config, utc_offset_minutes, connected);
  return g_display.renderTimedRegion(*page, region, runtime,
                                     epd::pageIdentityHash(config.page));
}

void configureKeyWake(const epd::DeviceConfig& config) {
  if (config.hardware.io12_mode != epd::Io12Mode::kKey) return;
  const uint32_t release_deadline = millis() + 2000U;
  while (digitalRead(epd::hardware::kUserKey) == LOW &&
         static_cast<int32_t>(millis() - release_deadline) < 0) {
    delay(10);
  }
#if CONFIG_IDF_TARGET_ESP32C3
  gpio_sleep_set_pull_mode(
      static_cast<gpio_num_t>(epd::hardware::kUserKey), GPIO_PULLUP_ONLY);
  esp_deep_sleep_enable_gpio_wakeup(
      1ULL << epd::hardware::kUserKey, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  rtc_gpio_pullup_dis(static_cast<gpio_num_t>(epd::hardware::kUserKey));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(epd::hardware::kUserKey));
  esp_sleep_enable_ext0_wakeup(
      static_cast<gpio_num_t>(epd::hardware::kUserKey), 0);
#endif
}

[[noreturn]] void sleepUntil(const epd::DeviceConfig& config,
                             uint64_t wake_at) {
  const uint64_t now = unixNow();
  const uint64_t delay_sec = wake_at > now ? wake_at - now : 1;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(delay_sec * 1000000ULL);
  configureKeyWake(config);
#ifndef EPD_TOOLKIT_RELEASE
  Serial.flush();
#endif
  esp_deep_sleep_start();
  abort();
}

uint64_t nextScheduledWake() {
  if (g_rtc_power.next_page_tick_at == 0) return g_rtc_power.next_sync_at;
  if (g_rtc_power.next_sync_at == 0) return g_rtc_power.next_page_tick_at;
  return std::min(g_rtc_power.next_sync_at, g_rtc_power.next_page_tick_at);
}

bool tryBatteryTimedFastPath(const epd::DeviceConfig& config) {
  const uint64_t now = unixNow();
  if (config.power.profile != epd::PowerProfile::kBattery ||
      esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER ||
      now < kValidUnixTime || g_rtc_power.magic != kRtcPowerMagic ||
      g_rtc_power.next_page_tick_at == 0 ||
      now < g_rtc_power.next_page_tick_at ||
      (g_rtc_power.next_sync_at != 0 && now >= g_rtc_power.next_sync_at)) {
    return false;
  }
  const uint32_t page_hash = epd::pageIdentityHash(config.page);
  if (g_rtc_power.page_hash != page_hash ||
      g_display.retainedPageHash() != page_hash) {
    return false;
  }
  epd::IPage* page = g_pages.find(config.page.id);
  if (page == nullptr || page->manifest().timed_region_count == 0) return false;
  const epd::TimedRegion& region = page->manifest().timed_regions[0];
  if (!renderTimedRegion(config, g_rtc_power.utc_offset_minutes, false)) {
    return false;
  }
  const epd::PresentResult result = g_display.present(config.display);
  g_rtc_power.next_page_tick_at = nextBoundary(now, region.interval_sec);
  TOOLKIT_LOG("power", String("clock-only wake result=") +
                           (result == epd::PresentResult::kFull
                                ? "full"
                                : result == epd::PresentResult::kPartial
                                      ? "partial"
                                      : "unchanged"));
  (void)result;
  sleepUntil(config, nextScheduledWake());
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
  renderCurrent(false);
  g_ble.stop();
  const uint64_t now = unixNow();
  g_rtc_power.magic = kRtcPowerMagic;
  g_rtc_power.page_hash = epd::pageIdentityHash(config.page);
  g_rtc_power.utc_offset_minutes = g_ble.utcOffsetMinutes();
  g_rtc_power.next_sync_at = now + config.power.wake_interval_sec;
  epd::IPage* page = g_pages.find(config.page.id);
  g_rtc_power.next_page_tick_at =
      page != nullptr && page->manifest().timed_region_count > 0
          ? nextBoundary(now, page->manifest().timed_regions[0].interval_sec)
          : 0;
  const uint64_t wake_at = nextScheduledWake();
  TOOLKIT_LOG("power", String("sync complete; next wake in ") +
                           (wake_at > now ? wake_at - now : 1) + "s");
  sleepUntil(config, wake_at);
}

void handleKey() {
  if (g_ble.config().hardware.io12_mode != epd::Io12Mode::kKey) return;
  const bool pressed = digitalRead(epd::hardware::kUserKey) == LOW;
  if (pressed == g_key_pressed || millis() - g_key_changed_at < 35U) return;
  g_key_changed_at = millis();
  g_key_pressed = pressed;
  if (!pressed) {
    TOOLKIT_LOG("input", "IO12 short press");
    g_ble.emitKeyPressed();
  }
}

void handleMainsPageTick() {
  const epd::DeviceConfig& config = g_ble.config();
  if (config.power.profile != epd::PowerProfile::kMains) return;
  const uint64_t now = unixNow();
  epd::IPage* page = g_pages.find(config.page.id);
  if (now < kValidUnixTime || page == nullptr ||
      page->manifest().timed_region_count == 0) {
    g_next_mains_page_tick = 0;
    return;
  }
  const epd::TimedRegion& region = page->manifest().timed_regions[0];
  if (g_next_mains_page_tick == 0) {
    g_next_mains_page_tick = nextBoundary(now, region.interval_sec);
    return;
  }
  if (now < g_next_mains_page_tick) return;
  if (renderTimedRegion(config, g_ble.utcOffsetMinutes(),
                        g_ble.sessionReady())) {
    g_display.present(config.display);
  } else {
    renderCurrent(false);
  }
  g_next_mains_page_tick = nextBoundary(now, region.interval_sec);
}

}  // namespace

void setup() {
  registerPages();
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
    TOOLKIT_LOG("security", "fresh v4 security state initialized");
  }
  if (config.hardware.io12_mode == epd::Io12Mode::kKey) {
    pinMode(epd::hardware::kUserKey, INPUT_PULLUP);
  }
  g_display.begin();
  tryBatteryTimedFastPath(config);

  String resource_error;
  if (!g_resources.load(resource_error)) {
    TOOLKIT_LOG("resource", String("load failed: ") + resource_error);
  }
  TOOLKIT_LOG("core", String("config revision=") + config.revision +
                          " page=" + config.page.id + " power=" +
                          (config.power.profile == epd::PowerProfile::kMains
                               ? "mains"
                               : "battery"));
  g_battery_mv = readBatteryMillivolts(config);
  if (!config.hardware.battery.enabled ||
      g_battery_mv >= config.hardware.battery.recovery_mv) {
    g_display.setLowBatteryLatched(false);
  } else if ((g_battery_mv > 0 &&
              g_battery_mv <= config.hardware.battery.critical_mv) ||
             (g_display.lowBatteryLatched() &&
              g_battery_mv < config.hardware.battery.recovery_mv)) {
    enterCriticalSleep(config);
  }
  const uint32_t passkey = 100000U + esp_random() % 900000U;
  if (!g_ble.begin(config, g_battery_mv, passkey)) {
    TOOLKIT_LOG("ble", "BLE v4 initialization failed");
  } else {
    TOOLKIT_LOG("ble", "BLE v4 service ready");
  }
  const bool deep_sleep_wake =
      esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  if (g_ble.owned() && !deep_sleep_wake) {
    renderCurrent(true);
  }
  g_last_battery_sample = millis();
}

void loop() {
  g_serial.loop();
  g_ble.loop();
  handleKey();
  handleMainsPageTick();

  uint32_t factory_code = 0;
  if (g_ble.takeFactoryCode(factory_code)) {
    g_display.renderFactoryResetConfirmation(factory_code);
    g_display.present(g_ble.config().display);
  }
  bool force_full = false;
  if (g_ble.takeRenderRequest(force_full)) {
    g_ble.emitDisplayStarted(force_full);
    const epd::PresentResult result = renderCurrent(force_full);
    g_ble.emitDisplayCompleted(
        result == epd::PresentResult::kFull
            ? "full"
            : result == epd::PresentResult::kPartial ? "partial" : "unchanged");
  }
  if (g_ble.config().hardware.battery.enabled &&
      millis() - g_last_battery_sample >= kBatterySampleIntervalMs) {
    g_last_battery_sample = millis();
    g_battery_mv = readBatteryMillivolts(g_ble.config());
    g_ble.updateBattery(g_battery_mv);
    if (g_battery_mv > 0 &&
        g_battery_mv <= g_ble.config().hardware.battery.critical_mv) {
      enterCriticalSleep(g_ble.config());
    }
  }
  String save_error;
  if (!g_resources.saveIfDue(unixNow(), false, save_error) &&
      millis() - g_last_storage_error_at >= 5000U) {
    g_last_storage_error_at = millis();
    TOOLKIT_LOG("resource", String("snapshot save failed: ") + save_error);
  }
  const bool factory_reset = g_ble.takeFactoryResetRequest();
  const bool restart = g_ble.takeRestartRequest();
  if (factory_reset || restart) {
    delay(250);
    ESP.restart();
  }
  if (g_ble.takeSleepRequest()) enterScheduledSleep(g_ble.config());
  delay(10);
}
