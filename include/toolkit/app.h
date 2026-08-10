#pragma once

#include <lvgl.h>

#include "toolkit/resource_store.h"

namespace epd {

struct RenderContext {
  uint64_t now = 0;
  uint16_t battery_mv = 0;
  bool battery_enabled = false;
  bool connected = false;
  int16_t utc_offset_minutes = 0;
};

class IRenderer {
 public:
  virtual ~IRenderer() = default;
  virtual const char* id() const = 0;
  virtual const char* schemaId() const = 0;
  virtual uint16_t schemaVersion() const = 0;
  virtual bool accepts(const ResourceRecord& resource) const;
  virtual void buildUi(lv_obj_t* root, const ResourceRecord* resource,
                       const RenderContext& context) = 0;
};

class RendererRegistry {
 public:
  explicit RendererRegistry(IRenderer& codex) : codex_(codex) {}
  IRenderer* find(const String& id);
  size_t size() const { return 1; }
  IRenderer& at(size_t) { return codex_; }

 private:
  IRenderer& codex_;
};

}  // namespace epd
