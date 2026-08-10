#include <unity.h>

#include <array>
#include <cstdint>
#include "toolkit/core_logic.h"

using epd::core::ConfigSlotChoice;
using epd::core::Rect;

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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dirty_rect_is_empty_for_identical_frames);
  RUN_TEST(test_dirty_rect_aligns_x_and_clips_right_edge);
  RUN_TEST(test_refresh_policy_thresholds);
  RUN_TEST(test_config_slot_marker_is_atomic_commit_point);
  return UNITY_END();
}
