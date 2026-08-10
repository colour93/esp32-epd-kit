#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace epd::core {

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;

  constexpr Rect() = default;
  constexpr Rect(int16_t x_value, int16_t y_value, int16_t width_value,
                 int16_t height_value)
      : x(x_value),
        y(y_value),
        width(width_value),
        height(height_value) {}

  constexpr bool empty() const { return width <= 0 || height <= 0; }
  constexpr uint32_t area() const {
    return empty() ? 0U : static_cast<uint32_t>(width) * height;
  }
};

inline Rect findDirtyRect(const uint8_t* before, const uint8_t* after,
                          uint16_t width, uint16_t height) {
  const uint16_t stride = (width + 7U) / 8U;
  int16_t min_x = static_cast<int16_t>(width);
  int16_t min_y = static_cast<int16_t>(height);
  int16_t max_x = -1;
  int16_t max_y = -1;

  for (uint16_t y = 0; y < height; ++y) {
    for (uint16_t byte_x = 0; byte_x < stride; ++byte_x) {
      const size_t index = static_cast<size_t>(y) * stride + byte_x;
      const uint8_t changed = before[index] ^ after[index];
      if (changed == 0) continue;

      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint16_t x = byte_x * 8U + bit;
        if (x >= width || (changed & (0x80U >> bit)) == 0) continue;
        min_x = std::min<int16_t>(min_x, x);
        max_x = std::max<int16_t>(max_x, x);
        min_y = std::min<int16_t>(min_y, y);
        max_y = std::max<int16_t>(max_y, y);
      }
    }
  }

  if (max_x < min_x || max_y < min_y) return {};

  const int16_t aligned_x1 = min_x & ~0x7;
  const int16_t aligned_x2 = std::min<int16_t>(width - 1, max_x | 0x7);
  return {aligned_x1, min_y, static_cast<int16_t>(aligned_x2 - aligned_x1 + 1),
          static_cast<int16_t>(max_y - min_y + 1)};
}

inline bool shouldFullRefresh(bool old_frame_valid, const Rect& dirty,
                              uint16_t screen_width, uint16_t screen_height,
                              uint8_t area_threshold_percent,
                              uint16_t partial_count,
                              uint16_t max_partial_count,
                              uint32_t full_age_seconds,
                              uint32_t max_full_age_seconds) {
  if (!old_frame_valid) return true;
  if (partial_count >= max_partial_count) return true;
  if (max_full_age_seconds > 0 && full_age_seconds >= max_full_age_seconds) return true;
  if (dirty.empty()) return false;
  const uint32_t total_area = static_cast<uint32_t>(screen_width) * screen_height;
  return dirty.area() * 100U >= total_area * area_threshold_percent;
}

enum class ConfigSlotChoice : uint8_t { kNone, kA, kB };

inline ConfigSlotChoice selectConfigSlot(bool a_valid, uint32_t a_sequence,
                                         bool b_valid, uint32_t b_sequence,
                                         uint8_t active_marker) {
  // The active marker is the commit point. A newer inactive slot can be a
  // verified write that lost power before the marker switch and must not win.
  if (active_marker == 0 && a_valid) return ConfigSlotChoice::kA;
  if (active_marker == 1 && b_valid) return ConfigSlotChoice::kB;
  if (a_valid && b_valid) {
    return a_sequence >= b_sequence ? ConfigSlotChoice::kA
                                    : ConfigSlotChoice::kB;
  }
  if (a_valid) return ConfigSlotChoice::kA;
  if (b_valid) return ConfigSlotChoice::kB;
  return ConfigSlotChoice::kNone;
}

}  // namespace epd::core
