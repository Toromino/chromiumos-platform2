// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "power_manager/common/clock.h"
#include "power_manager/powerd/system/internal_backlight.h"

namespace power_manager {
namespace system {

class InternalBacklightTest : public ::testing::Test {
 public:
  InternalBacklightTest() {}
  virtual ~InternalBacklightTest() {}

  virtual void SetUp() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    test_path_ = temp_dir_.path();
  }

  // Create files to make the given directory look like it is a sysfs backlight
  // dir.
  void PopulateBacklightDir(const base::FilePath& path,
                            int64 brightness,
                            int64 max_brightness,
                            int64 actual_brightness) {
    CHECK(base::CreateDirectory(path));

    std::string str = base::StringPrintf("%" PRId64 "\n", brightness);
    ASSERT_EQ(str.size(),
              file_util::WriteFile(
                  path.Append(InternalBacklight::kBrightnessFilename),
                  str.data(), str.size()));

    str = base::StringPrintf("%" PRId64 "\n", max_brightness);
    ASSERT_EQ(str.size(),
              file_util::WriteFile(
                  path.Append(InternalBacklight::kMaxBrightnessFilename),
                  str.data(), str.size()));

    if (actual_brightness >= 0) {
      str = base::StringPrintf("%" PRId64 "\n", actual_brightness);
      ASSERT_EQ(str.size(),
                file_util::WriteFile(
                    path.Append(InternalBacklight::kActualBrightnessFilename),
                    str.data(), str.size()));
    }
  }

  // Returns the value from the "brightness" file in |directory|.
  // -1 is returned on error.
  int64 ReadBrightness(const base::FilePath& directory) {
    std::string data;
    base::FilePath file =
        directory.Append(InternalBacklight::kBrightnessFilename);
    if (!base::ReadFileToString(file, &data)) {
      LOG(ERROR) << "Unable to read data from " << file.value();
      return -1;
    }
    int64 level = 0;
    TrimWhitespaceASCII(data, TRIM_TRAILING, &data);
    if (!base::StringToInt64(data, &level)) {
      LOG(ERROR) << "Unable to parse \"" << level << "\" from " << file.value();
      return -1;
    }
    return level;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath test_path_;
};

// A basic test of functionality
TEST_F(InternalBacklightTest, BasicTest) {
  base::FilePath this_test_path = test_path_.Append("basic_test");
  const int64 kBrightness = 128;
  const int64 kMaxBrightness = 255;
  const int64 kActualBrightness = 127;

  base::FilePath my_path = this_test_path.Append("pwm-backlight");
  PopulateBacklightDir(my_path, kBrightness, kMaxBrightness, kActualBrightness);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*"));
  EXPECT_EQ(kActualBrightness, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(kMaxBrightness, backlight.GetMaxBrightnessLevel());
}

// Make sure things work OK when there is no actual_brightness file.
TEST_F(InternalBacklightTest, NoActualBrightnessTest) {
  base::FilePath this_test_path =
      test_path_.Append("no_actual_brightness_test");
  const int64 kBrightness = 128;
  const int64 kMaxBrightness = 255;

  base::FilePath my_path = this_test_path.Append("pwm-backlight");
  PopulateBacklightDir(my_path, kBrightness, kMaxBrightness, -1);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*"));
  EXPECT_EQ(kBrightness, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(kMaxBrightness, backlight.GetMaxBrightnessLevel());
}

// Test that we pick the one with the greatest granularity
TEST_F(InternalBacklightTest, GranularityTest) {
  base::FilePath this_test_path = test_path_.Append("granularity_test");

  // Make sure the middle one is the most granular so we're not just
  // getting lucky.  Middle in terms of order created and alphabet, since I
  // don't know how enumaration might be happening.
  base::FilePath a_path = this_test_path.Append("a");
  PopulateBacklightDir(a_path, 10, 127, 11);
  base::FilePath b_path = this_test_path.Append("b");
  PopulateBacklightDir(b_path, 20, 255, 21);
  base::FilePath c_path = this_test_path.Append("c");
  PopulateBacklightDir(c_path, 30, 63, 31);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*"));
  EXPECT_EQ(21, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(255, backlight.GetMaxBrightnessLevel());
}

// Test ignore directories starting with a "."
TEST_F(InternalBacklightTest, NoDotDirsTest) {
  base::FilePath this_test_path = test_path_.Append("no_dot_dirs_test");

  // We'll just create one dir and it will have a dot in it.  Then, we can
  // be sure that we didn't just get luckly...
  base::FilePath my_path = this_test_path.Append(".pwm-backlight");
  PopulateBacklightDir(my_path, 128, 255, 127);

  InternalBacklight backlight;
  EXPECT_FALSE(backlight.Init(this_test_path, "*"));
}

// Test that the glob is working correctly for searching for backlight dirs.
TEST_F(InternalBacklightTest, GlobTest) {
  base::FilePath this_test_path = test_path_.Append("glob_test");

  // Purposely give my::kbd_backlight a lower "max_level" than
  // .no::kbd_backlight so that we know that dirs starting with a "." are
  // ignored.
  base::FilePath my_path = this_test_path.Append("my::kbd_backlight");
  PopulateBacklightDir(my_path, 1, 2, -1);

  base::FilePath ignore1_path = this_test_path.Append("ignore1");
  PopulateBacklightDir(ignore1_path, 3, 4, -1);

  base::FilePath ignore2_path = this_test_path.Append(".no::kbd_backlight");
  PopulateBacklightDir(ignore2_path, 5, 6, -1);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*:kbd_backlight"));

  EXPECT_EQ(1, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(2, backlight.GetMaxBrightnessLevel());
}

TEST_F(InternalBacklightTest, Transitions) {
  const int kMaxBrightness = 100;
  base::FilePath backlight_dir = test_path_.Append("transitions_test");
  PopulateBacklightDir(backlight_dir, 50, kMaxBrightness, 50);

  InternalBacklight backlight;
  const base::TimeTicks kStartTime = base::TimeTicks::FromInternalValue(10000);
  backlight.clock()->set_current_time_for_testing(kStartTime);
  ASSERT_TRUE(backlight.Init(test_path_, "*"));

  // An instant transition to the maximum level shouldn't use a timer.
  backlight.SetBrightnessLevel(kMaxBrightness, base::TimeDelta());
  EXPECT_FALSE(backlight.transition_timer_is_running());
  EXPECT_EQ(kMaxBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kMaxBrightness, backlight.GetCurrentBrightnessLevel());

  // Start a transition to the backlight's halfway point.
  const int64 kHalfBrightness = kMaxBrightness / 2;
  const base::TimeDelta kDuration = base::TimeDelta::FromMilliseconds(1000);
  backlight.SetBrightnessLevel(kHalfBrightness, kDuration);

  // If the timeout fires at this point, we should still be at the maximum
  // level.
  EXPECT_TRUE(backlight.transition_timer_is_running());
  EXPECT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  EXPECT_EQ(kMaxBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kMaxBrightness, backlight.GetCurrentBrightnessLevel());

  // Let half of the transition duration pass.
  const base::TimeTicks kMidpointTime = kStartTime + kDuration / 2;
  backlight.clock()->set_current_time_for_testing(kMidpointTime);
  EXPECT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  const int64 kMidpointBrightness = (kMaxBrightness + kHalfBrightness) / 2;
  EXPECT_EQ(kMidpointBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kMidpointBrightness, backlight.GetCurrentBrightnessLevel());

  // At the end of the transition, we should return false to cancel the timeout.
  const base::TimeTicks kEndTime = kStartTime + kDuration;
  backlight.clock()->set_current_time_for_testing(kEndTime);
  EXPECT_FALSE(backlight.TriggerTransitionTimeoutForTesting());
  EXPECT_FALSE(backlight.transition_timer_is_running());
  EXPECT_EQ(kHalfBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kHalfBrightness, backlight.GetCurrentBrightnessLevel());
}

}  // namespace system
}  // namespace power_manager
