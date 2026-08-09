#include "toolkit/app.h"

namespace epd {

IApp* AppRegistry::find(const String& id) {
  return id == codex_app_.manifest().id ? &codex_app_ : nullptr;
}

}  // namespace epd

