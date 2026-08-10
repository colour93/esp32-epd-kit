#include "toolkit/app.h"

namespace epd {

bool IRenderer::accepts(const ResourceRecord& resource) const {
  return resource.schema_id == schemaId() &&
         resource.schema_version == schemaVersion();
}

IRenderer* RendererRegistry::find(const String& id) {
  return id == codex_.id() ? &codex_ : nullptr;
}

}  // namespace epd
