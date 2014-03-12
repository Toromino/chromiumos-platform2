// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/keyboard_backlight_controller.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/backlight_interface.h"

namespace power_manager {
namespace policy {

namespace {

// Default control values for the user percent.
const double kUserPercentDim = 10.0;
const double kUserPercentMax = 100.0;
const double kUserPercentMin = 0.0;

// This is how long after a video playing message is received we should wait
// until reverting to the not playing state. If another message is received in
// this interval the timeout is reset. The browser should be sending these
// messages ~5 seconds when video is playing.
const int64 kVideoTimeoutIntervalMs = 7000;

// Returns the total duration for |style|.
base::TimeDelta GetTransitionDuration(
    BacklightController::TransitionStyle style) {
  switch (style) {
    case BacklightController::TRANSITION_INSTANT:
      return base::TimeDelta();
    case BacklightController::TRANSITION_FAST:
      return base::TimeDelta::FromMilliseconds(kFastBacklightTransitionMs);
    case BacklightController::TRANSITION_SLOW:
      return base::TimeDelta::FromMilliseconds(kSlowBacklightTransitionMs);
    default:
      NOTREACHED() << "Unhandled transition style " << style;
      return base::TimeDelta();
  }
}

}  // namespace

KeyboardBacklightController::TestApi::TestApi(
    KeyboardBacklightController* controller)
  : controller_(controller) {
}

KeyboardBacklightController::TestApi::~TestApi() {}

void KeyboardBacklightController::TestApi::TriggerVideoTimeout() {
  CHECK(controller_->video_timer_.IsRunning());
  controller_->video_timer_.Stop();
  controller_->HandleVideoTimeout();
}

KeyboardBacklightController::KeyboardBacklightController()
    : backlight_(NULL),
      prefs_(NULL),
      display_backlight_controller_(NULL),
      session_state_(SESSION_STOPPED),
      dimmed_for_inactivity_(false),
      off_for_inactivity_(false),
      shutting_down_(false),
      docked_(false),
      fullscreen_video_playing_(false),
      max_level_(0),
      current_level_(0),
      user_percent_dim_(kUserPercentDim),
      user_percent_max_(kUserPercentMax),
      user_percent_min_(kUserPercentMin),
      user_step_index_(-1),
      percent_for_ambient_light_(100.0),
      ignore_ambient_light_(false),
      num_als_adjustments_(0),
      num_user_adjustments_(0),
      display_brightness_is_zero_(false) {
}

KeyboardBacklightController::~KeyboardBacklightController() {
  if (display_backlight_controller_)
    display_backlight_controller_->RemoveObserver(this);
}

void KeyboardBacklightController::Init(
    system::BacklightInterface* backlight,
    PrefsInterface* prefs,
    system::AmbientLightSensorInterface* sensor,
    BacklightController* display_backlight_controller) {
  backlight_ = backlight;
  prefs_ = prefs;

  display_backlight_controller_ = display_backlight_controller;
  if (display_backlight_controller_)
    display_backlight_controller_->AddObserver(this);

  if (sensor) {
    ambient_light_handler_.reset(new AmbientLightHandler(sensor, this));
    ambient_light_handler_->set_name("keyboard");
  }

  max_level_ = backlight_->GetMaxBrightnessLevel();
  current_level_ = backlight_->GetCurrentBrightnessLevel();

  ReadPrefs();

  if (ambient_light_handler_.get()) {
    ambient_light_handler_->Init(prefs_, kKeyboardBacklightAlsLimitsPref,
                                 kKeyboardBacklightAlsStepsPref,
                                 LevelToPercent(current_level_));
  }

  LOG(INFO) << "Backlight has range [0, " << max_level_ << "] with initial "
            << "level " << current_level_;
}

void KeyboardBacklightController::AddObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void KeyboardBacklightController::RemoveObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void KeyboardBacklightController::HandleVideoActivity(bool is_fullscreen) {
  // Ignore fullscreen video that's reported when the user isn't logged in;
  // it may be triggered by animations on the login screen.
  if (is_fullscreen && session_state_ == SESSION_STOPPED)
    is_fullscreen = false;

  if (is_fullscreen != fullscreen_video_playing_) {
    VLOG(1) << "Fullscreen video "
            << (is_fullscreen ? "started" : "went non-fullscreen");
    fullscreen_video_playing_ = is_fullscreen;
    UpdateState();
  }

  video_timer_.Stop();
  if (is_fullscreen) {
    video_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kVideoTimeoutIntervalMs),
        this, &KeyboardBacklightController::HandleVideoTimeout);
  }
}

void KeyboardBacklightController::HandlePowerSourceChange(PowerSource source) {}

void KeyboardBacklightController::HandleDisplayModeChange(DisplayMode mode) {}

void KeyboardBacklightController::HandleSessionStateChange(SessionState state) {
  session_state_ = state;
  if (state == SESSION_STARTED) {
    num_als_adjustments_ = 0;
    num_user_adjustments_ = 0;
  }
}

void KeyboardBacklightController::HandlePowerButtonPress() {}

void KeyboardBacklightController::HandleUserActivity(UserActivityType type) {}

void KeyboardBacklightController::HandlePolicyChange(
    const PowerManagementPolicy& policy) {}

void KeyboardBacklightController::HandleChromeStart() {}

void KeyboardBacklightController::SetDimmedForInactivity(bool dimmed) {
  if (dimmed == dimmed_for_inactivity_)
    return;
  dimmed_for_inactivity_ = dimmed;
  UpdateState();
}

void KeyboardBacklightController::SetOffForInactivity(bool off) {
  if (off == off_for_inactivity_)
    return;
  off_for_inactivity_ = off;
  UpdateState();
}

void KeyboardBacklightController::SetSuspended(bool suspended) {}

void KeyboardBacklightController::SetShuttingDown(bool shutting_down) {
  if (shutting_down == shutting_down_)
    return;
  shutting_down_ = shutting_down;
  UpdateState();
}

void KeyboardBacklightController::SetDocked(bool docked) {
  if (docked == docked_)
    return;
  docked_ = docked;
  UpdateState();
}

bool KeyboardBacklightController::GetBrightnessPercent(double* percent) {
  DCHECK(percent);
  *percent = LevelToPercent(current_level_);
  return *percent >= 0.0;
}

bool KeyboardBacklightController::SetUserBrightnessPercent(
    double percent,
    TransitionStyle style) {
  // There's currently no UI for setting the keyboard backlight brightness
  // to arbitrary levels; the user is instead just given the option of
  // increasing or decreasing the brightness between pre-defined levels.
  return false;
}

bool KeyboardBacklightController::IncreaseUserBrightness() {
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ < static_cast<int>(user_steps_.size()) - 1)
    user_step_index_++;
  num_user_adjustments_++;

  return UpdateUndimmedBrightness(TRANSITION_FAST,
                                  BRIGHTNESS_CHANGE_USER_INITIATED);
}

bool KeyboardBacklightController::DecreaseUserBrightness(bool allow_off) {
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ > (allow_off ? 0 : 1))
    user_step_index_--;
  num_user_adjustments_++;

  return UpdateUndimmedBrightness(TRANSITION_FAST,
                                  BRIGHTNESS_CHANGE_USER_INITIATED);
}

int KeyboardBacklightController::GetNumAmbientLightSensorAdjustments() const {
  return num_als_adjustments_;
}

int KeyboardBacklightController::GetNumUserAdjustments() const {
  return num_user_adjustments_;
}

void KeyboardBacklightController::SetBrightnessPercentForAmbientLight(
    double brightness_percent,
    AmbientLightHandler::BrightnessChangeCause cause) {
  if (ignore_ambient_light_)
    return;
  percent_for_ambient_light_ = brightness_percent;
  TransitionStyle transition =
      cause == AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT ?
      TRANSITION_SLOW : TRANSITION_FAST;
  if (UpdateUndimmedBrightness(transition, BRIGHTNESS_CHANGE_AUTOMATED) &&
      cause == AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT)
    num_als_adjustments_++;
}

void KeyboardBacklightController::OnBrightnessChanged(
    double brightness_percent,
    BacklightController::BrightnessChangeCause cause,
    BacklightController* source) {
  DCHECK_EQ(source, display_backlight_controller_);

  bool zero = brightness_percent <= kEpsilon;
  if (zero != display_brightness_is_zero_) {
    display_brightness_is_zero_ = zero;
    UpdateState();
  }
}

void KeyboardBacklightController::ReadPrefs() {
  ReadLimitsPrefs(kKeyboardBacklightUserLimitsPref,
                  &user_percent_min_, &user_percent_dim_, &user_percent_max_);
  ReadUserStepsPref();
  prefs_->GetBool(kDisableALSPref, &ignore_ambient_light_);
}

void KeyboardBacklightController::ReadLimitsPrefs(const std::string& pref_name,
                                                  double* min,
                                                  double* dim,
                                                  double* max) {
  std::string input_str;
  if (prefs_->GetString(pref_name, &input_str)) {
    std::vector<std::string> inputs;
    base::SplitString(input_str, '\n', &inputs);
    double temp_min, temp_dim, temp_max;
    if (inputs.size() == 3 &&
        base::StringToDouble(inputs[0], &temp_min) &&
        base::StringToDouble(inputs[1], &temp_dim) &&
        base::StringToDouble(inputs[2], &temp_max)) {
      *min = temp_min;
      *dim = temp_dim;
      *max = temp_max;
    } else {
      ReplaceSubstringsAfterOffset(&input_str, 0, "\n", "\\n");
      LOG(ERROR) << "Failed to parse pref " << pref_name
                 << " with contents: \"" << input_str << "\"";
    }
  } else {
    LOG(ERROR) << "Failed to read pref " << pref_name;
  }
}

void KeyboardBacklightController::ReadUserStepsPref() {
  std::string input_str;
  user_steps_.clear();
  if (prefs_->GetString(kKeyboardBacklightUserStepsPref, &input_str)) {
    std::vector<std::string> lines;
    base::SplitString(input_str, '\n', &lines);
    for (std::vector<std::string>::iterator iter = lines.begin();
         iter != lines.end(); ++iter) {
      double new_step = 0.0;
      if (!base::StringToDouble(*iter, &new_step))
        LOG(ERROR) << "Skipping line in user step pref: \"" << *iter << "\"";
      else
        user_steps_.push_back(new_step);
    }
  } else {
    LOG(ERROR) << "Failed to read user steps file";
  }

  if (user_steps_.empty()) {
    VLOG(1) << "No user steps read; inserting default steps";
    user_steps_.push_back(user_percent_min_);
    user_steps_.push_back(user_percent_dim_);
    user_steps_.push_back(user_percent_max_);
  }
}

void KeyboardBacklightController::HandleVideoTimeout() {
  if (fullscreen_video_playing_)
    VLOG(1) << "Fullscreen video stopped";
  fullscreen_video_playing_ = false;
  UpdateState();
}

int64 KeyboardBacklightController::PercentToLevel(double percent) const {
  if (max_level_ == 0)
    return -1;
  percent = std::max(std::min(percent, 100.0), 0.0);
  return lround(static_cast<double>(max_level_) * percent / 100.0);
}

double KeyboardBacklightController::LevelToPercent(int64 level) const {
  if (max_level_ == 0)
    return -1.0;
  level = std::max(std::min(level, max_level_), static_cast<int64>(0));
  return static_cast<double>(level) * 100.0 / max_level_;
}

void KeyboardBacklightController::InitUserStepIndex() {
  if (user_step_index_ != -1)
    return;

  // Capping in case we are still using the firmware set value, which can be
  // larger then our expected range.
  double percent = std::min(LevelToPercent(current_level_), user_percent_max_);
  // Select the nearest step to the current backlight level and adjust the
  // target percent in line with it.
  double percent_delta = 2 * user_percent_max_;
  size_t i;
  for (i = 0; i < user_steps_.size(); i++) {
    double temp_delta = fabs(percent - user_steps_[i]);
    if (temp_delta < percent_delta) {
      percent_delta = temp_delta;
      user_step_index_ = i;
    }
  }
  CHECK(percent_delta < 2 * user_percent_max_);
}

double KeyboardBacklightController::GetUndimmedPercent() const {
  return user_step_index_ != -1 ? user_steps_[user_step_index_] :
      percent_for_ambient_light_;
}

bool KeyboardBacklightController::UpdateUndimmedBrightness(
    TransitionStyle transition,
    BrightnessChangeCause cause) {
  if (shutting_down_|| fullscreen_video_playing_ || off_for_inactivity_ ||
      dimmed_for_inactivity_ || docked_)
    return false;

  return ApplyBrightnessPercent(GetUndimmedPercent(), transition, cause);
}

bool KeyboardBacklightController::UpdateState() {
  double percent = 0.0;
  TransitionStyle transition = TRANSITION_SLOW;
  bool use_user = user_step_index_ != -1;

  if (shutting_down_ || docked_) {
    percent = 0.0;
    transition = TRANSITION_INSTANT;
  } else if ((!use_user && fullscreen_video_playing_) ||
             (!use_user && display_brightness_is_zero_) ||
             off_for_inactivity_) {
    percent = use_user ? user_percent_min_ :
        ambient_light_handler_->min_brightness_percent();
  } else if (dimmed_for_inactivity_) {
    double dimmed_percent = use_user ? user_percent_dim_ :
        ambient_light_handler_->dimmed_brightness_percent();
    percent = std::min(dimmed_percent, GetUndimmedPercent());
  } else {
    percent = GetUndimmedPercent();
  }

  return ApplyBrightnessPercent(percent, transition,
                                BRIGHTNESS_CHANGE_AUTOMATED);
}

bool KeyboardBacklightController::ApplyBrightnessPercent(
    double percent,
    TransitionStyle transition,
    BrightnessChangeCause cause) {
  int64 level = PercentToLevel(percent);
  if (level == current_level_)
    return false;

  base::TimeDelta interval = GetTransitionDuration(transition);
  VLOG(1) << "Setting brightness to " << level << " (" << percent
          << "%) over " << interval.InMilliseconds() << " ms";
  if (!backlight_->SetBrightnessLevel(level, interval)) {
    LOG(ERROR) << "Failed to set brightness";
    return false;
  }

  current_level_ = level;
  FOR_EACH_OBSERVER(BacklightControllerObserver, observers_,
                    OnBrightnessChanged(percent, cause, this));
  return true;
}

}  // namespace policy
}  // namespace power_manager
