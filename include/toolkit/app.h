#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "toolkit/config.h"
#include "toolkit/core_logic.h"
#include "toolkit/resource_store.h"

namespace epd {

using Rect = core::Rect;

enum class SlotStatus : uint8_t { kActive, kReserved };
enum class ResourceState : uint8_t { kMissing, kInvalid, kStale, kFresh };

struct PageSlot {
  const char* id;
  const char* schema_id;
  uint16_t schema_version;
  bool required;
  SlotStatus status;
};

struct TimedRegion {
  const char* id;
  Rect bounds;
  uint32_t interval_sec;
};

struct PageManifest {
  const char* id;
  const char* title;
  const PageSlot* slots;
  size_t slot_count;
  const TimedRegion* timed_regions;
  size_t timed_region_count;
};

struct RuntimeContext {
  uint64_t now = 0;
  uint16_t battery_mv = 0;
  bool battery_enabled = false;
  bool connected = false;
  int16_t utc_offset_minutes = 0;
};

struct SlotResource {
  const PageSlot* slot = nullptr;
  const char* resource_key = nullptr;
  const ResourceRecord* resource = nullptr;
  ResourceState state = ResourceState::kMissing;
};

class PageResources {
 public:
  PageResources(const PageManifest& manifest, const PageSettings& settings,
                const ResourceStore& resources, uint64_t now)
      : manifest_(manifest), settings_(settings), resources_(resources), now_(now) {}

  SlotResource get(const char* slot_id) const;
  bool usesKey(const String& key) const;
  uint32_t freshnessSignature() const;

 private:
  const PageManifest& manifest_;
  const PageSettings& settings_;
  const ResourceStore& resources_;
  uint64_t now_;
};

struct PageContext {
  const PageResources& resources;
  const RuntimeContext& runtime;
};

class IPage {
 public:
  virtual ~IPage() = default;
  virtual const PageManifest& manifest() const = 0;
  virtual void buildUi(lv_obj_t* root, const PageContext& context) = 0;
  virtual void buildTimedRegion(const char* id, lv_obj_t* root,
                                const RuntimeContext& context) = 0;
};

class PageRegistry {
 public:
  static constexpr size_t kCapacity = 8;

  bool add(IPage& page, String& error);
  IPage* find(const String& id) const;
  size_t size() const { return size_; }
  IPage& at(size_t index) const { return *pages_[index]; }

 private:
  IPage* pages_[kCapacity]{};
  size_t size_ = 0;
};

const PageSlot* findPageSlot(const PageManifest& manifest, const String& id);
const TimedRegion* findTimedRegion(const PageManifest& manifest,
                                  const String& id);
bool validatePageSettings(const PageSettings& settings,
                          const PageRegistry& registry,
                          const ResourceStore& resources, String& error);
uint32_t pageIdentityHash(const PageSettings& settings);
const char* resourceStateCode(ResourceState state);

}  // namespace epd
