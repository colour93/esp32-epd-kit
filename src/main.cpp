#include <Arduino.h>

#include <NimBLEDevice.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>

#include "toolkit/app.h"
#include "toolkit/ble_provisioning.h"
#include "toolkit/codex_usage_app.h"
#include "toolkit/codex_usage_client.h"
#include "toolkit/config_store.h"
#include "toolkit/display_manager.h"
#include "toolkit/hardware.h"
#include "toolkit/network_manager.h"
#include "toolkit/usage_snapshot_store.h"

namespace {

constexpr uint32_t kRuntimeRtcMagic = 0x52554E31U;  // "RUN1"
constexpr uint32_t kCriticalSleepSeconds = 6U * 3600U;

struct RuntimeRtcState {
  uint32_t magic;
  uint8_t consecutive_failures;
  bool critical_battery_latched;
  uint16_t reserved;
};

RTC_DATA_ATTR RuntimeRtcState g_runtime_state{};

enum class BootAction : uint8_t {
  kNormal,
  kConfiguration,
  kFactoryResetPreparation,
};

epd::ConfigStore g_config_store;
epd::UsageSnapshotStore g_snapshot_store;
epd::NetworkManager g_network;
epd::CodexUsageClient g_codex_client;
epd::CodexUsageApp g_codex_app;
epd::AppRegistry g_registry(g_codex_app);
epd::DisplayManager g_display;
epd::BleProvisioningService g_ble(g_config_store, g_network, g_registry,
                                  g_display);

#ifndef EPD_TOOLKIT_RELEASE
void debugLog(const String& value) {
  Serial.print("[toolkit] ");
  Serial.println(value);
}
#define TOOLKIT_LOG(value) debugLog(value)
#else
#define TOOLKIT_LOG(value) ((void)0)
#endif

bool isColdOrBrownoutReset() {
  const esp_reset_reason_t reason = esp_reset_reason();
  return reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT ||
         reason == ESP_RST_UNKNOWN;
}

void initializeRuntimeState() {
  if (g_runtime_state.magic != kRuntimeRtcMagic || isColdOrBrownoutReset()) {
    g_runtime_state = {};
    g_runtime_state.magic = kRuntimeRtcMagic;
  }
}

uint16_t readBatteryMillivolts() {
  pinMode(epd::hardware::kBatteryAdc, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(epd::hardware::kBatteryAdc, ADC_11db);
  delay(5);
  analogReadMilliVolts(epd::hardware::kBatteryAdc);  // Discard first sample.

  uint16_t samples[9]{};
  for (uint8_t index = 0; index < 9; ++index) {
    samples[index] = analogReadMilliVolts(epd::hardware::kBatteryAdc);
    delay(2);
  }
  std::sort(std::begin(samples), std::end(samples));
  // The Waveshare schematic exposes one third of VBAT on GPIO36.
  const uint32_t millivolts = static_cast<uint32_t>(samples[4]) * 3U;
  return millivolts >= 2500U && millivolts <= 5000U
             ? static_cast<uint16_t>(millivolts)
             : 0;
}

uint8_t batteryPercent(uint16_t millivolts) {
  if (millivolts == 0) return 100;
  if (millivolts <= 3300) return 0;
  if (millivolts >= 4200) return 100;
  return static_cast<uint8_t>((millivolts - 3300U) * 100U / 900U);
}

BootAction readBootAction() {
  pinMode(epd::hardware::kUserKey, INPUT_PULLUP);
  if (digitalRead(epd::hardware::kUserKey) != LOW) return BootAction::kNormal;

  const uint32_t pressed_at = millis();
  while (digitalRead(epd::hardware::kUserKey) == LOW &&
         millis() - pressed_at < 10000U) {
    delay(20);
  }
  const uint32_t held_ms = millis() - pressed_at;
  if (held_ms >= 10000U) return BootAction::kFactoryResetPreparation;
  if (held_ms >= 3000U) return BootAction::kConfiguration;
  return BootAction::kNormal;  // Short press requests the normal immediate query.
}

void setFailure(epd::SyncStatus status, const String& detail,
                uint16_t battery_mv) {
  epd::CodexAppState& state = g_codex_app.codexState();
  state.battery_mv = battery_mv;
  state.usage.status = status;
  state.usage.status_detail = detail;
}

uint32_t configuredBackoff(const epd::DeviceConfig& config) {
  const size_t index =
      g_runtime_state.consecutive_failures == 0
          ? 0
          : std::min<size_t>(g_runtime_state.consecutive_failures - 1, 3);
  return config.power.offline_backoff_sec[index];
}

void waitForKeyRelease() {
  const uint32_t started_at = millis();
  while (digitalRead(epd::hardware::kUserKey) == LOW &&
         millis() - started_at < 2000U) {
    delay(10);
  }
}

[[noreturn]] void enterDeepSleep(uint32_t seconds, bool timer_enabled) {
  g_network.disconnect();
  g_ble.stop();
  waitForKeyRelease();

  TOOLKIT_LOG(String("deep sleep: timer=") +
              (timer_enabled ? "on" : "off") + ", seconds=" + seconds);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // GPIO12 is a boot strap. Do not retain an RTC pull-up across deep sleep;
  // the board's key circuit provides the idle level and a pressed key is LOW.
  rtc_gpio_pullup_dis(static_cast<gpio_num_t>(epd::hardware::kUserKey));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(epd::hardware::kUserKey));
  esp_sleep_enable_ext0_wakeup(
      static_cast<gpio_num_t>(epd::hardware::kUserKey), 0);
  if (timer_enabled) {
    const uint64_t micros = static_cast<uint64_t>(std::max<uint32_t>(seconds, 60U)) *
                            1000000ULL;
    esp_sleep_enable_timer_wakeup(micros);
  }
#ifndef EPD_TOOLKIT_RELEASE
  Serial.flush();
#endif
  esp_deep_sleep_start();
  abort();
}

bool runBleSession(epd::DeviceConfig& config, uint16_t battery_mv,
                   bool reset_preparation) {
  const bool was_configured = config.isConfigured();
  const uint32_t passkey = 100000U + esp_random() % 900000U;
  if (reset_preparation) {
    g_display.renderFactoryResetConfirmation();
    g_display.present(config.display);
    delay(1800);
  }
  g_display.renderPairing(passkey, was_configured);
  g_display.present(config.display);

  const uint32_t advertising_seconds =
      was_configured ? config.power.ble_window_sec : 10U * 60U;
  TOOLKIT_LOG(String("BLE session: configured=") +
              (was_configured ? "yes" : "no") +
              ", window_sec=" + advertising_seconds);
  if (!g_ble.begin(config, batteryPercent(battery_mv), passkey,
                   advertising_seconds)) {
    TOOLKIT_LOG("BLE initialization failed");
    return false;
  }

  while (g_ble.active()) {
    g_ble.loop();
    if (g_ble.refreshRequested() || g_ble.factoryResetRequested()) break;
    delay(10);
  }
  const bool refresh_requested = g_ble.refreshRequested();
  const bool factory_reset_requested = g_ble.factoryResetRequested();
  if (g_ble.configCommitted()) config = g_ble.currentConfig();
  TOOLKIT_LOG(String("BLE session ended: committed=") +
              (g_ble.configCommitted() ? "yes" : "no") +
              ", refresh=" + (refresh_requested ? "yes" : "no"));
  g_ble.stop();

  if (factory_reset_requested) {
    delay(250);
    ESP.restart();
  }
  return refresh_requested || g_ble.configCommitted();
}

void renderAndPresent(const epd::DeviceConfig& config, bool force_full) {
  g_display.renderApp(g_codex_app);
  [[maybe_unused]] const epd::PresentResult result =
      g_display.present(config.display, force_full);
#ifndef EPD_TOOLKIT_RELEASE
  const char* name = result == epd::PresentResult::kFull
                         ? "full"
                         : result == epd::PresentResult::kPartial ? "partial"
                                                                 : "unchanged";
  TOOLKIT_LOG("display: " + String(name));
#endif
}

}  // namespace

void setup() {
#ifndef EPD_TOOLKIT_RELEASE
  Serial.begin(115200);
  delay(30);
#endif
  TOOLKIT_LOG(String("boot: firmware=") + EPD_TOOLKIT_VERSION +
              ", reset_reason=" + static_cast<int>(esp_reset_reason()) +
              ", wakeup_cause=" +
              static_cast<int>(esp_sleep_get_wakeup_cause()));
  initializeRuntimeState();
  const bool force_full = isColdOrBrownoutReset();
  const BootAction boot_action = readBootAction();
  const uint16_t battery_mv = readBatteryMillivolts();
  TOOLKIT_LOG(String("battery_mv=") + battery_mv + ", boot_action=" +
              static_cast<int>(boot_action));

  epd::DeviceConfig config;
  const bool config_loaded = g_config_store.load(config);
  if (!config_loaded) config = epd::DeviceConfig{};
  TOOLKIT_LOG(String("config: loaded=") +
              (config_loaded ? "yes" : "no") + ", configured=" +
              (config.isConfigured() ? "yes" : "no"));

  g_display.begin();
  g_snapshot_store.load(g_codex_app.codexState().usage);
  g_codex_app.codexState().battery_mv = battery_mv;

  if (battery_mv > 0 && battery_mv <= config.battery.critical_mv) {
    g_runtime_state.critical_battery_latched = true;
  } else if (battery_mv == 0 || battery_mv >= config.battery.recovery_mv) {
    g_runtime_state.critical_battery_latched = false;
  }
  if (g_runtime_state.critical_battery_latched) {
    TOOLKIT_LOG("critical battery latch active; radio disabled");
    setFailure(epd::SyncStatus::kLowBattery, "radio disabled", battery_mv);
    g_display.renderLowBattery(battery_mv);
    g_display.present(config.display, force_full);
    enterDeepSleep(kCriticalSleepSeconds, true);
  }

  const bool needs_configuration = !config_loaded || !config.isConfigured();
  const bool requested_configuration =
      boot_action == BootAction::kConfiguration ||
      boot_action == BootAction::kFactoryResetPreparation;
  bool refresh_after_ble = false;
  if (needs_configuration || requested_configuration) {
    refresh_after_ble = runBleSession(
        config, battery_mv,
        boot_action == BootAction::kFactoryResetPreparation);
    if (!config.isConfigured()) {
      // Provisioning resumes only after a power cycle or physical key press.
      enterDeepSleep(config.power.poll_interval_sec, false);
    }
    if (!refresh_after_ble && requested_configuration) {
      renderAndPresent(config, false);
      enterDeepSleep(config.power.poll_interval_sec, true);
    }
  }

  epd::IApp* app = g_registry.find(config.active_app);
  String validation_error;
  if (app == nullptr || !app->validateConfig(config, validation_error)) {
    setFailure(epd::SyncStatus::kAuthExpired, validation_error, battery_mv);
    renderAndPresent(config, force_full);
    enterDeepSleep(config.power.poll_interval_sec, false);
  }

  if (epd::NetworkManager::clockValid() && config.codex.expires_at > 0 &&
      static_cast<uint64_t>(time(nullptr)) >= config.codex.expires_at) {
    setFailure(epd::SyncStatus::kAuthExpired, "reauthorization required",
               battery_mv);
    renderAndPresent(config, force_full);
    enterDeepSleep(config.power.poll_interval_sec, false);
  }

  uint32_t next_wake_seconds = config.power.poll_interval_sec;
  bool timer_enabled = true;
  TOOLKIT_LOG("Wi-Fi connect started");
  if (!g_network.connect(config.wifi)) {
    if (g_runtime_state.consecutive_failures < UINT8_MAX) {
      ++g_runtime_state.consecutive_failures;
    }
    setFailure(epd::SyncStatus::kOffline, g_network.lastError(), battery_mv);
    next_wake_seconds = configuredBackoff(config);
    TOOLKIT_LOG("Wi-Fi failed: " + g_network.lastError());
  } else if (!g_network.syncClock(config.device)) {
    if (g_runtime_state.consecutive_failures < UINT8_MAX) {
      ++g_runtime_state.consecutive_failures;
    }
    setFailure(epd::SyncStatus::kTimeError, g_network.lastError(), battery_mv);
    next_wake_seconds = configuredBackoff(config);
    TOOLKIT_LOG("time sync failed: " + g_network.lastError());
  } else {
    TOOLKIT_LOG("network ready; application update started");
    epd::AppContext context{config, g_network, g_codex_client, battery_mv};
    const epd::UpdateResult update = app->update(context);
    TOOLKIT_LOG(String("application update: status=") +
                epd::syncStatusCode(update.status));
    if (update.status == epd::SyncStatus::kOk) {
      g_runtime_state.consecutive_failures = 0;
      next_wake_seconds = app->nextWakeSeconds(update);
      String snapshot_error;
      if (!g_snapshot_store.saveIfDue(g_codex_app.codexState().usage,
                                      static_cast<uint64_t>(time(nullptr)),
                                      snapshot_error)) {
        TOOLKIT_LOG("usage snapshot write failed: " + snapshot_error);
      }
    } else if (update.status == epd::SyncStatus::kAuthExpired) {
      // A stale successful snapshot stays visible, but periodic radio wakeups stop.
      timer_enabled = false;
    } else {
      if (g_runtime_state.consecutive_failures < UINT8_MAX) {
        ++g_runtime_state.consecutive_failures;
      }
      next_wake_seconds = configuredBackoff(config);
    }
  }

  g_network.disconnect();
  renderAndPresent(config, force_full);
  enterDeepSleep(next_wake_seconds, timer_enabled);
}

void loop() { delay(1000); }
