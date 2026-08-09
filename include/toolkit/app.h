#pragma once

#include <lvgl.h>

#include "toolkit/codex_usage_client.h"
#include "toolkit/config.h"
#include "toolkit/network_manager.h"
#include "toolkit/usage_model.h"

namespace epd {

struct AppManifest {
  const char* id;
  const char* name;
  const char* version;
  bool requires_network;
  bool has_secrets;

  AppManifest(const char* app_id, const char* app_name,
              const char* app_version, bool network, bool secrets)
      : id(app_id),
        name(app_name),
        version(app_version),
        requires_network(network),
        has_secrets(secrets) {}
};

struct AppState {
  virtual ~AppState() = default;
};

struct AppContext {
  const DeviceConfig& config;
  NetworkManager& network;
  CodexUsageClient& codex_client;
  uint16_t battery_mv;

  AppContext(const DeviceConfig& device_config, NetworkManager& network_service,
             CodexUsageClient& usage_client, uint16_t millivolts)
      : config(device_config),
        network(network_service),
        codex_client(usage_client),
        battery_mv(millivolts) {}
};

struct UpdateResult {
  SyncStatus status = SyncStatus::kNever;
  bool state_changed = false;
  uint32_t next_wake_sec = 300;

  UpdateResult() = default;
  UpdateResult(SyncStatus update_status, bool changed, uint32_t wake_seconds)
      : status(update_status),
        state_changed(changed),
        next_wake_sec(wake_seconds) {}
};

class IApp {
 public:
  virtual ~IApp() = default;
  virtual AppManifest manifest() const = 0;
  virtual bool validateConfig(const DeviceConfig& config, String& error) const = 0;
  virtual UpdateResult update(AppContext& context) = 0;
  virtual void buildUi(lv_obj_t* root, const AppState& state) = 0;
  virtual uint32_t nextWakeSeconds(const UpdateResult& result) const = 0;
  virtual AppState& state() = 0;
};

class AppRegistry {
 public:
  explicit AppRegistry(IApp& codex_app) : codex_app_(codex_app) {}
  IApp* find(const String& id);
  size_t size() const { return 1; }
  IApp& at(size_t index) { return codex_app_; }

 private:
  IApp& codex_app_;
};

}  // namespace epd
