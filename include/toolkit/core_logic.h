#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

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

inline uint8_t remainingPercent(float used_percent) {
  const float clamped = std::max(0.0F, std::min(100.0F, used_percent));
  return static_cast<uint8_t>(100.0F - clamped + 0.5F);
}

enum class WindowKind : uint8_t { kUnknown, kFiveHours, kWeekly };

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

inline WindowKind identifyWindow(uint32_t seconds) {
  if (seconds == 18000U) return WindowKind::kFiveHours;
  if (seconds == 604800U) return WindowKind::kWeekly;
  return WindowKind::kUnknown;
}

inline uint32_t retryDelaySeconds(uint8_t consecutive_failures) {
  constexpr uint32_t kBackoff[] = {300U, 900U, 1800U, 3600U};
  const size_t index = consecutive_failures == 0
                           ? 0
                           : std::min<size_t>(consecutive_failures - 1,
                                              sizeof(kBackoff) / sizeof(kBackoff[0]) - 1);
  return kBackoff[index];
}

inline bool parseIpv4(std::string_view text, uint32_t& result) {
  if (text.empty() || text.size() > 15) return false;
  result = 0;
  size_t index = 0;
  for (uint8_t part = 0; part < 4; ++part) {
    if (index >= text.size()) return false;
    uint16_t value = 0;
    uint8_t digits = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
      value = value * 10U + static_cast<uint8_t>(text[index] - '0');
      if (++digits > 3 || value > 255U) return false;
      ++index;
    }
    if (digits == 0) return false;
    result = (result << 8U) | value;
    if (part < 3) {
      if (index >= text.size() || text[index++] != '.') return false;
    } else if (index != text.size()) {
      return false;
    }
  }
  return true;
}

inline bool isUnicastIpv4(std::string_view text) {
  uint32_t address = 0;
  if (!parseIpv4(text, address)) return false;
  const uint8_t first_octet = address >> 24U;
  return address != 0 && address != UINT32_MAX && first_octet > 0 &&
         first_octet < 224;
}

inline bool isContiguousSubnetMask(std::string_view text,
                                   uint32_t* parsed_mask = nullptr) {
  uint32_t mask = 0;
  if (!parseIpv4(text, mask) || mask == 0) return false;
  const uint32_t inverse = ~mask;
  if ((inverse & (inverse + 1U)) != 0) return false;
  if (parsed_mask != nullptr) *parsed_mask = mask;
  return true;
}

inline bool isValidStaticIpv4(std::string_view address_text,
                              std::string_view gateway_text,
                              std::string_view subnet_text,
                              std::string_view dns1_text,
                              std::string_view dns2_text) {
  uint32_t address = 0;
  uint32_t gateway = 0;
  uint32_t mask = 0;
  if (!parseIpv4(address_text, address) ||
      !parseIpv4(gateway_text, gateway) ||
      !isUnicastIpv4(address_text) || !isUnicastIpv4(gateway_text) ||
      !isContiguousSubnetMask(subnet_text, &mask) ||
      !isUnicastIpv4(dns1_text) ||
      (!dns2_text.empty() && !isUnicastIpv4(dns2_text))) {
    return false;
  }

  // /31 and /32 have no ordinary host range for this gateway-based station
  // configuration. The local address and gateway must be distinct hosts on
  // the same subnet and cannot be the subnet or directed broadcast address.
  const uint32_t host_mask = ~mask;
  if (host_mask < 3U) return false;
  const uint32_t network = address & mask;
  const uint32_t broadcast = network | host_mask;
  return (gateway & mask) == network && address != gateway &&
         address != network && address != broadcast && gateway != network &&
         gateway != broadcast;
}

inline bool isValidHttpProxyHost(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  for (const unsigned char value : host) {
    if (value <= 0x20U || value >= 0x7FU || value == '/' || value == ':' ||
        value == '@' || value == '#') {
      return false;
    }
  }
  return true;
}

}  // namespace epd::core
