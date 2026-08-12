#include "toolkit/app.h"

namespace epd {
namespace {

uint32_t hashByte(uint32_t hash, uint8_t value) {
  return (hash ^ value) * 16777619U;
}

uint32_t hashText(uint32_t hash, const String& value) {
  for (size_t index = 0; index < value.length(); ++index) {
    hash = hashByte(hash, static_cast<uint8_t>(value[index]));
  }
  return hashByte(hash, 0);
}

bool stale(const ResourceRecord& resource, uint64_t now) {
  return resource.ttl_sec > 0 && now > resource.updated_at &&
         now - resource.updated_at > resource.ttl_sec;
}

bool validIdentifier(const char* value, size_t maximum) {
  if (value == nullptr) return false;
  const String text(value);
  if (text.isEmpty() || text.length() > maximum) return false;
  for (size_t index = 0; index < text.length(); ++index) {
    const uint8_t byte = static_cast<uint8_t>(text[index]);
    if (byte < 0x20U || byte == 0x7FU) return false;
  }
  return true;
}

}  // namespace

const PageSlot* findPageSlot(const PageManifest& manifest, const String& id) {
  for (size_t index = 0; index < manifest.slot_count; ++index) {
    if (id == manifest.slots[index].id) return &manifest.slots[index];
  }
  return nullptr;
}

const PageWidget* findPageWidget(const PageSlot& slot, const String& id) {
  for (size_t index = 0; index < slot.widget_count; ++index) {
    if (id == slot.widgets[index].id) return &slot.widgets[index];
  }
  return nullptr;
}

const TimedRegion* findTimedRegion(const PageManifest& manifest,
                                  const String& id) {
  for (size_t index = 0; index < manifest.timed_region_count; ++index) {
    if (id == manifest.timed_regions[index].id) {
      return &manifest.timed_regions[index];
    }
  }
  return nullptr;
}

SlotResource PageResources::get(const char* slot_id) const {
  SlotResource resolved;
  resolved.slot = findPageSlot(manifest_, slot_id);
  if (resolved.slot == nullptr || resolved.slot->status == SlotStatus::kReserved) {
    return resolved;
  }
  const PageBinding* binding = settings_.findBinding(slot_id);
  if (binding == nullptr) return resolved;
  resolved.widget = binding->widget_id.isEmpty()
                        ? (resolved.slot->widget_count > 0
                               ? &resolved.slot->widgets[0]
                               : nullptr)
                        : findPageWidget(*resolved.slot, binding->widget_id);
  resolved.resource_key = binding->resource_key.c_str();
  resolved.resource = resources_.get(binding->resource_key);
  if (resolved.widget == nullptr) {
    resolved.state = ResourceState::kInvalid;
  } else if (resolved.resource == nullptr) {
    return resolved;
  } else if (resolved.resource->schema_id != resolved.widget->schema_id ||
             resolved.resource->schema_version !=
                 resolved.widget->schema_version) {
    resolved.state = ResourceState::kInvalid;
  } else if (stale(*resolved.resource, now_)) {
    resolved.state = ResourceState::kStale;
  } else {
    resolved.state = ResourceState::kFresh;
  }
  return resolved;
}

bool PageResources::usesKey(const String& key) const {
  for (size_t index = 0; index < settings_.binding_count; ++index) {
    if (settings_.bindings[index].resource_key == key) return true;
  }
  return false;
}

uint32_t PageResources::freshnessSignature() const {
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < manifest_.slot_count; ++index) {
    const PageSlot& slot = manifest_.slots[index];
    hash = hashText(hash, slot.id);
    const SlotResource resolved = get(slot.id);
    hash = hashByte(hash, static_cast<uint8_t>(resolved.state));
    if (resolved.resource_key != nullptr) {
      hash = hashText(hash, String(resolved.resource_key));
    }
    if (resolved.widget != nullptr) {
      hash = hashText(hash, String(resolved.widget->id));
    }
  }
  return hash;
}

bool PageRegistry::add(IPage& page, String& error) {
  const PageManifest& manifest = page.manifest();
  const String id = manifest.id == nullptr ? "" : manifest.id;
  if (!validIdentifier(manifest.id, kPageIdMaxBytes)) {
    error = "page id is invalid";
    return false;
  }
  if (find(id) != nullptr) {
    error = "duplicate page id: " + id;
    return false;
  }
  if (size_ >= kCapacity) {
    error = "page registry is full";
    return false;
  }
  if ((manifest.slot_count > 0 && manifest.slots == nullptr) ||
      (manifest.timed_region_count > 0 && manifest.timed_regions == nullptr)) {
    error = "page manifest arrays are invalid: " + id;
    return false;
  }
  for (size_t index = 0; index < manifest.slot_count; ++index) {
    const PageSlot& slot = manifest.slots[index];
    if (!validIdentifier(slot.id, kSlotIdMaxBytes)) {
      error = "page slot id is invalid: " + id;
      return false;
    }
    for (size_t other = 0; other < index; ++other) {
      if (String(slot.id) == manifest.slots[other].id) {
        error = "duplicate page slot: " + String(slot.id);
        return false;
      }
    }
    if (slot.status == SlotStatus::kActive &&
        (!validIdentifier(slot.title, kPageIdMaxBytes) ||
         slot.widgets == nullptr || slot.widget_count == 0)) {
      error = "active page slot widgets are invalid: " + String(slot.id);
      return false;
    }
    if (slot.status == SlotStatus::kReserved &&
        (slot.required || slot.widgets != nullptr || slot.widget_count != 0)) {
      error = "reserved page slot must not declare widgets: " + String(slot.id);
      return false;
    }
    for (size_t widget_index = 0; widget_index < slot.widget_count;
         ++widget_index) {
      const PageWidget& widget = slot.widgets[widget_index];
      if (!validIdentifier(widget.id, kWidgetIdMaxBytes) ||
          !validIdentifier(widget.title, kPageIdMaxBytes) ||
          !validIdentifier(widget.schema_id, kPageIdMaxBytes) ||
          widget.schema_version == 0) {
        error = "page widget is invalid: " + String(slot.id);
        return false;
      }
      for (size_t other = 0; other < widget_index; ++other) {
        if (String(widget.id) == slot.widgets[other].id) {
          error = "duplicate page widget: " + String(widget.id);
          return false;
        }
      }
    }
  }
  for (size_t index = 0; index < manifest.timed_region_count; ++index) {
    const TimedRegion& region = manifest.timed_regions[index];
    if (!validIdentifier(region.id, kSlotIdMaxBytes) ||
        region.interval_sec == 0 || region.bounds.empty()) {
      error = "timed region is invalid: " + id;
      return false;
    }
    for (size_t other = 0; other < index; ++other) {
      if (String(region.id) == manifest.timed_regions[other].id) {
        error = "duplicate timed region: " + String(region.id);
        return false;
      }
    }
  }
  pages_[size_++] = &page;
  return true;
}

IPage* PageRegistry::find(const String& id) const {
  for (size_t index = 0; index < size_; ++index) {
    if (id == pages_[index]->manifest().id) return pages_[index];
  }
  return nullptr;
}

bool validatePageSettings(const PageSettings& settings,
                          const PageRegistry& registry,
                          const ResourceStore& resources, String& error) {
  IPage* page = registry.find(settings.id);
  if (page == nullptr) {
    error = "unknown page id";
    return false;
  }
  const PageManifest& manifest = page->manifest();
  for (size_t index = 0; index < settings.binding_count; ++index) {
    const PageBinding& binding = settings.bindings[index];
    const PageSlot* slot = findPageSlot(manifest, binding.slot_id);
    if (slot == nullptr) {
      error = "unknown page slot: " + binding.slot_id;
      return false;
    }
    if (slot->status == SlotStatus::kReserved) {
      error = "reserved page slot cannot be bound: " + binding.slot_id;
      return false;
    }
    const PageWidget* widget = binding.widget_id.isEmpty()
                                   ? (slot->widget_count > 0 ? &slot->widgets[0]
                                                           : nullptr)
                                   : findPageWidget(*slot, binding.widget_id);
    if (widget == nullptr) {
      error = "unknown widget for page slot: " + binding.slot_id;
      return false;
    }
    for (size_t other = index + 1; other < settings.binding_count; ++other) {
      if (binding.slot_id == settings.bindings[other].slot_id) {
        error = "duplicate page binding: " + binding.slot_id;
        return false;
      }
    }
    const ResourceRecord* resource = resources.get(binding.resource_key);
    if (resource != nullptr &&
        (resource->schema_id != widget->schema_id ||
         resource->schema_version != widget->schema_version)) {
      error = "resource schema does not match slot: " + binding.slot_id;
      return false;
    }
  }
  for (size_t index = 0; index < manifest.slot_count; ++index) {
    const PageSlot& slot = manifest.slots[index];
    if (slot.status == SlotStatus::kActive && slot.required &&
        settings.findBinding(slot.id) == nullptr) {
      error = "required page slot is not bound: " + String(slot.id);
      return false;
    }
  }
  return true;
}

uint32_t pageIdentityHash(const PageSettings& settings) {
  uint32_t hash = hashText(2166136261U, settings.id);
  for (size_t index = 0; index < settings.binding_count; ++index) {
    hash = hashText(hash, settings.bindings[index].slot_id);
    hash = hashText(hash, settings.bindings[index].widget_id);
    hash = hashText(hash, settings.bindings[index].resource_key);
  }
  return hash;
}

const char* resourceStateCode(ResourceState state) {
  switch (state) {
    case ResourceState::kMissing: return "missing";
    case ResourceState::kInvalid: return "invalid";
    case ResourceState::kStale: return "stale";
    case ResourceState::kFresh: return "fresh";
  }
  return "invalid";
}

}  // namespace epd
