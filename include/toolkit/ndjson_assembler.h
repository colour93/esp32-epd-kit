#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace epd::core {

enum class NdjsonFeedStatus : uint8_t { kOk, kTimeout, kTooLarge };

struct NdjsonFeedResult {
  NdjsonFeedStatus status = NdjsonFeedStatus::kOk;
  std::vector<std::string> lines;
};

class NdjsonAssembler {
 public:
  explicit NdjsonAssembler(size_t max_message_bytes = 8192,
                           uint32_t timeout_ms = 5000)
      : max_message_bytes_(max_message_bytes), timeout_ms_(timeout_ms) {}

  NdjsonFeedResult feed(const uint8_t* data, size_t length, uint32_t now_ms) {
    NdjsonFeedResult result;
    if (expire(now_ms)) {
      result.status = NdjsonFeedStatus::kTimeout;
      return result;
    }

    for (size_t index = 0; index < length; ++index) {
      const char value = static_cast<char>(data[index]);
      if (value == '\n') {
        if (!buffer_.empty() && buffer_.back() == '\r') buffer_.pop_back();
        if (!buffer_.empty()) result.lines.push_back(buffer_);
        clear();
        continue;
      }
      if (buffer_.empty()) started_at_ms_ = now_ms;
      if (buffer_.size() >= max_message_bytes_) {
        clear();
        result.lines.clear();
        result.status = NdjsonFeedStatus::kTooLarge;
        return result;
      }
      buffer_.push_back(value);
    }
    return result;
  }

  bool expire(uint32_t now_ms) {
    if (buffer_.empty() || now_ms - started_at_ms_ <= timeout_ms_) return false;
    clear();
    return true;
  }

  void clear() {
    buffer_.clear();
    started_at_ms_ = 0;
  }

  bool empty() const { return buffer_.empty(); }
  size_t size() const { return buffer_.size(); }

 private:
  size_t max_message_bytes_;
  uint32_t timeout_ms_;
  uint32_t started_at_ms_ = 0;
  std::string buffer_;
};

}  // namespace epd::core
