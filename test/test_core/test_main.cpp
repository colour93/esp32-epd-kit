#include <unity.h>

#include <array>
#include <cstdint>
#include <string>

#include "toolkit/core_logic.h"
#include "toolkit/ndjson_assembler.h"

using epd::core::ConfigSlotChoice;
using epd::core::NdjsonFeedStatus;
using epd::core::Rect;
using epd::core::WindowKind;

void setUp() {}
void tearDown() {}

void test_dirty_rect_is_empty_for_identical_frames() {
  const std::array<uint8_t, 4> frame = {0xFF, 0xFF, 0xFF, 0xFF};
  const Rect dirty = epd::core::findDirtyRect(frame.data(), frame.data(), 10, 2);
  TEST_ASSERT_TRUE(dirty.empty());
}

void test_dirty_rect_aligns_x_and_clips_right_edge() {
  const std::array<uint8_t, 4> before = {0xFF, 0xFF, 0xFF, 0xFF};
  std::array<uint8_t, 4> after = before;
  after[3] &= static_cast<uint8_t>(~0x40U);  // x=9, y=1.
  const Rect dirty = epd::core::findDirtyRect(before.data(), after.data(), 10, 2);
  TEST_ASSERT_EQUAL_INT16(8, dirty.x);
  TEST_ASSERT_EQUAL_INT16(1, dirty.y);
  TEST_ASSERT_EQUAL_INT16(2, dirty.width);
  TEST_ASSERT_EQUAL_INT16(1, dirty.height);
}

void test_refresh_policy_thresholds() {
  TEST_ASSERT_TRUE(epd::core::shouldFullRefresh(false, Rect(0, 0, 1, 1),
                                                100, 100, 40, 0, 12, 0,
                                                86400));
  TEST_ASSERT_TRUE(epd::core::shouldFullRefresh(true, Rect(0, 0, 40, 100),
                                                100, 100, 40, 0, 12, 0,
                                                86400));
  TEST_ASSERT_TRUE(epd::core::shouldFullRefresh(true, Rect(0, 0, 1, 1),
                                                100, 100, 40, 12, 12, 0,
                                                86400));
  TEST_ASSERT_TRUE(epd::core::shouldFullRefresh(true, Rect(0, 0, 1, 1),
                                                100, 100, 40, 0, 12, 86400,
                                                86400));
  TEST_ASSERT_TRUE(epd::core::shouldFullRefresh(true, Rect(), 100, 100, 40,
                                                12, 12, 0, 86400));
  TEST_ASSERT_TRUE(epd::core::shouldFullRefresh(true, Rect(), 100, 100, 40,
                                                0, 12, 86400, 86400));
  TEST_ASSERT_FALSE(epd::core::shouldFullRefresh(true, Rect(), 100, 100, 40,
                                                 0, 12, 0, 86400));
}

void test_usage_windows_and_remaining_percent() {
  TEST_ASSERT_EQUAL(static_cast<int>(WindowKind::kFiveHours),
                    static_cast<int>(epd::core::identifyWindow(18000)));
  TEST_ASSERT_EQUAL(static_cast<int>(WindowKind::kWeekly),
                    static_cast<int>(epd::core::identifyWindow(604800)));
  TEST_ASSERT_EQUAL(static_cast<int>(WindowKind::kUnknown),
                    static_cast<int>(epd::core::identifyWindow(3600)));
  TEST_ASSERT_EQUAL_UINT8(65, epd::core::remainingPercent(35.0F));
  TEST_ASSERT_EQUAL_UINT8(100, epd::core::remainingPercent(-1.0F));
  TEST_ASSERT_EQUAL_UINT8(0, epd::core::remainingPercent(120.0F));
}

void test_retry_backoff_caps_at_one_hour() {
  TEST_ASSERT_EQUAL_UINT32(300, epd::core::retryDelaySeconds(0));
  TEST_ASSERT_EQUAL_UINT32(300, epd::core::retryDelaySeconds(1));
  TEST_ASSERT_EQUAL_UINT32(900, epd::core::retryDelaySeconds(2));
  TEST_ASSERT_EQUAL_UINT32(1800, epd::core::retryDelaySeconds(3));
  TEST_ASSERT_EQUAL_UINT32(3600, epd::core::retryDelaySeconds(4));
  TEST_ASSERT_EQUAL_UINT32(3600, epd::core::retryDelaySeconds(250));
}

void test_config_slot_marker_is_atomic_commit_point() {
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigSlotChoice::kA),
                    static_cast<int>(epd::core::selectConfigSlot(
                        true, 4, true, 5, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigSlotChoice::kB),
                    static_cast<int>(epd::core::selectConfigSlot(
                        true, 4, true, 5, 1)));
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigSlotChoice::kB),
                    static_cast<int>(epd::core::selectConfigSlot(
                        false, 0, true, 5, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigSlotChoice::kNone),
                    static_cast<int>(epd::core::selectConfigSlot(
                        false, 0, false, 0, 0)));
}

void test_static_ipv4_validation() {
  TEST_ASSERT_TRUE(epd::core::isValidStaticIpv4(
      "192.168.50.42", "192.168.50.1", "255.255.255.0", "1.1.1.1", ""));
  TEST_ASSERT_TRUE(epd::core::isValidStaticIpv4(
      "10.20.30.40", "10.20.30.1", "255.255.255.0", "8.8.8.8",
      "1.0.0.1"));
  TEST_ASSERT_FALSE(epd::core::isValidStaticIpv4(
      "192.168.50.999", "192.168.50.1", "255.255.255.0", "1.1.1.1", ""));
  TEST_ASSERT_FALSE(epd::core::isValidStaticIpv4(
      "192.168.50.42", "192.168.51.1", "255.255.255.0", "1.1.1.1", ""));
  TEST_ASSERT_FALSE(epd::core::isValidStaticIpv4(
      "192.168.50.255", "192.168.50.1", "255.255.255.0", "1.1.1.1", ""));
  TEST_ASSERT_FALSE(epd::core::isValidStaticIpv4(
      "192.168.50.42", "192.168.50.1", "255.0.255.0", "1.1.1.1", ""));
  TEST_ASSERT_FALSE(epd::core::isValidStaticIpv4(
      "192.168.50.42", "192.168.50.41", "255.255.255.254", "1.1.1.1", ""));
  TEST_ASSERT_FALSE(epd::core::isValidStaticIpv4(
      "192.168.50.42", "192.168.50.1", "255.255.255.0", "224.0.0.1", ""));
}

void test_http_proxy_host_validation() {
  TEST_ASSERT_TRUE(epd::core::isValidHttpProxyHost("proxy.lan"));
  TEST_ASSERT_TRUE(epd::core::isValidHttpProxyHost("192.168.50.10"));
  TEST_ASSERT_FALSE(epd::core::isValidHttpProxyHost(""));
  TEST_ASSERT_FALSE(epd::core::isValidHttpProxyHost("http://proxy.lan"));
  TEST_ASSERT_FALSE(epd::core::isValidHttpProxyHost("proxy.lan:8080"));
  TEST_ASSERT_FALSE(epd::core::isValidHttpProxyHost("[2001:db8::1]"));
  TEST_ASSERT_FALSE(epd::core::isValidHttpProxyHost("proxy lan"));
}

void test_ndjson_reassembles_twenty_byte_fragments() {
  epd::core::NdjsonAssembler assembler;
  const std::string message =
      "{\"v\":1,\"id\":7,\"op\":\"config.patch\",\"args\":{}}\n";
  size_t line_count = 0;
  for (size_t offset = 0; offset < message.size(); offset += 20) {
    const size_t length = std::min<size_t>(20, message.size() - offset);
    const auto result = assembler.feed(
        reinterpret_cast<const uint8_t*>(message.data() + offset), length,
        static_cast<uint32_t>(100 + offset));
    TEST_ASSERT_EQUAL(static_cast<int>(NdjsonFeedStatus::kOk),
                      static_cast<int>(result.status));
    for (const std::string& line : result.lines) {
      ++line_count;
      TEST_ASSERT_EQUAL_STRING(message.substr(0, message.size() - 1).c_str(),
                               line.c_str());
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(line_count));
  TEST_ASSERT_TRUE(assembler.empty());
}

void test_ndjson_timeout_and_oversize_clear_buffer() {
  epd::core::NdjsonAssembler timeout_assembler(8192, 5000);
  const uint8_t fragment[] = {'{', '"', 'v', '"'};
  timeout_assembler.feed(fragment, sizeof(fragment), 100);
  TEST_ASSERT_FALSE(timeout_assembler.expire(5100));
  TEST_ASSERT_TRUE(timeout_assembler.expire(5101));
  TEST_ASSERT_TRUE(timeout_assembler.empty());

  epd::core::NdjsonAssembler small_assembler(8, 5000);
  const std::string too_large = "123456789";
  const auto result = small_assembler.feed(
      reinterpret_cast<const uint8_t*>(too_large.data()), too_large.size(), 1);
  TEST_ASSERT_EQUAL(static_cast<int>(NdjsonFeedStatus::kTooLarge),
                    static_cast<int>(result.status));
  TEST_ASSERT_TRUE(small_assembler.empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dirty_rect_is_empty_for_identical_frames);
  RUN_TEST(test_dirty_rect_aligns_x_and_clips_right_edge);
  RUN_TEST(test_refresh_policy_thresholds);
  RUN_TEST(test_usage_windows_and_remaining_percent);
  RUN_TEST(test_retry_backoff_caps_at_one_hour);
  RUN_TEST(test_config_slot_marker_is_atomic_commit_point);
  RUN_TEST(test_static_ipv4_validation);
  RUN_TEST(test_http_proxy_host_validation);
  RUN_TEST(test_ndjson_reassembles_twenty_byte_fragments);
  RUN_TEST(test_ndjson_timeout_and_oversize_clear_buffer);
  return UNITY_END();
}
