// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdarg>

#include <gtest/gtest.h>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/time.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/state_controller.h"

namespace power_manager {
namespace policy {

namespace {

// Strings returned by TestDelegate::GetActions() to describe various
// actions that were requested.
const char kScreenDim[] = "dim";
const char kScreenOff[] = "off";
const char kScreenLock[] = "lock";
const char kScreenUndim[] = "undim";
const char kScreenOn[] = "on";
const char kSuspend[] = "suspend";
const char kStopSession[] = "logout";
const char kShutDown[] = "shutdown";

// String returned by TestDelegate::GetActions() if no actions were
// requested.
const char kNoActions[] = "";

// Joins a sequence of strings describing actions (e.g. kScreenDim) such
// that they can be compared against a string returned by
// TestDelegate::GetActions().  The list of actions must be terminated by a
// NULL pointer.
std::string JoinActions(const char* action, ...) {
  std::string actions;

  va_list arg_list;
  va_start(arg_list, action);
  while (action) {
    if (!actions.empty())
      actions += ",";
    actions += action;
    action = va_arg(arg_list, const char*);
  }
  va_end(arg_list);
  return actions;
}

// StateController::Delegate implementation that records requested actions.
class TestDelegate : public StateController::Delegate {
 public:
  TestDelegate()
      : usb_input_device_connected_(false),
        oobe_completed_(true) {
  }
  ~TestDelegate() {}

  void set_usb_input_device_connected(bool connected) {
    usb_input_device_connected_ = connected;
  }
  void set_oobe_completed(bool completed) {
    oobe_completed_ = completed;
  }

  // Returns a comma-separated string describing the actions that were
  // requested since the previous call to GetActions() (i.e. results are
  // non-repeatable).
  std::string GetActions() {
    std::string actions = actions_;
    actions_.clear();
    return actions;
  }

  // StateController::Delegate overrides:
  virtual bool IsUsbInputDeviceConnected() OVERRIDE {
    return usb_input_device_connected_;
  }
  virtual bool IsOobeCompleted() OVERRIDE { return oobe_completed_; }
  virtual void DimScreen() OVERRIDE { AppendAction(kScreenDim); }
  virtual void UndimScreen() OVERRIDE { AppendAction(kScreenUndim); }
  virtual void TurnScreenOff() OVERRIDE { AppendAction(kScreenOff); }
  virtual void TurnScreenOn() OVERRIDE { AppendAction(kScreenOn); }
  virtual void LockScreen() OVERRIDE { AppendAction(kScreenLock); }
  virtual void Suspend() OVERRIDE { AppendAction(kSuspend); }
  virtual void StopSession() OVERRIDE { AppendAction(kStopSession); }
  virtual void ShutDown() OVERRIDE { AppendAction(kShutDown); }

 private:
  void AppendAction(const std::string& action) {
    if (!actions_.empty())
      actions_ += ",";
    actions_ += action;
  }

  // Should IsUsbInputDeviceConnected() return true?
  bool usb_input_device_connected_;

  // Should IsOobeCompleted() return true?
  bool oobe_completed_;

  std::string actions_;

  DISALLOW_COPY_AND_ASSIGN(TestDelegate);
};

}  // namespace

class StateControllerTest : public testing::Test {
 public:
  StateControllerTest()
      : controller_(&delegate_, &prefs_),
        test_api_(&controller_),
        now_(base::TimeTicks::FromInternalValue(1000)),
        default_ac_suspend_delay_(base::TimeDelta::FromSeconds(120)),
        default_ac_screen_off_delay_(base::TimeDelta::FromSeconds(100)),
        default_ac_screen_dim_delay_(base::TimeDelta::FromSeconds(90)),
        default_battery_suspend_delay_(base::TimeDelta::FromSeconds(60)),
        default_battery_screen_off_delay_(base::TimeDelta::FromSeconds(40)),
        default_battery_screen_dim_delay_(base::TimeDelta::FromSeconds(30)),
        default_screen_lock_delay_(base::TimeDelta::FromSeconds(110)),
        default_disable_idle_suspend_(0),
        default_lock_on_idle_suspend_(1),
        default_require_usb_input_device_to_suspend_(0),
        default_keep_screen_on_for_audio_(0),
        initial_power_source_(StateController::POWER_AC),
        initial_lid_state_(StateController::LID_OPEN),
        initial_session_state_(StateController::SESSION_STARTED),
        initial_display_mode_(StateController::DISPLAY_NORMAL) {
  }

 protected:
  void SetMillisecondPref(const std::string& name, base::TimeDelta value) {
    CHECK(prefs_.SetInt64(name, value.InMilliseconds()));
  }

  // Sets values in |prefs_| based on |default_*| members and initializes
  // |controller_|.
  void Init() {
    SetMillisecondPref(kPluggedSuspendMsPref, default_ac_suspend_delay_);
    SetMillisecondPref(kPluggedOffMsPref, default_ac_screen_off_delay_);
    SetMillisecondPref(kPluggedDimMsPref, default_ac_screen_dim_delay_);
    SetMillisecondPref(kUnpluggedSuspendMsPref, default_battery_suspend_delay_);
    SetMillisecondPref(kUnpluggedOffMsPref, default_battery_screen_off_delay_);
    SetMillisecondPref(kUnpluggedDimMsPref, default_battery_screen_dim_delay_);
    SetMillisecondPref(kLockMsPref, default_screen_lock_delay_);
    CHECK(prefs_.SetInt64(kDisableIdleSuspendPref,
                          default_disable_idle_suspend_));
    CHECK(prefs_.SetInt64(kLockOnIdleSuspendPref,
                          default_lock_on_idle_suspend_));
    CHECK(prefs_.SetInt64(kRequireUsbInputDeviceToSuspendPref,
                          default_require_usb_input_device_to_suspend_));
    CHECK(prefs_.SetInt64(kKeepBacklightOnForAudioPref,
                          default_keep_screen_on_for_audio_));

    test_api_.SetCurrentTime(now_);
    controller_.Init(initial_power_source_, initial_lid_state_,
                     initial_session_state_, initial_display_mode_);
  }

  // Advances |now_| by |interval_|.
  void AdvanceTime(base::TimeDelta interval) {
    now_ += interval;
    test_api_.SetCurrentTime(now_);
  }

  // Checks that |controller_|'s timeout is scheduled for |now_| and then
  // runs it.  Returns false if the timeout isn't scheduled or is scheduled
  // for a different time.
  bool TriggerTimeout() {
    base::TimeTicks timeout_time = test_api_.GetTimeoutTime();
    if (timeout_time == base::TimeTicks()) {
      LOG(ERROR) << "Ignoring request to trigger unscheduled timeout at "
                 << now_.ToInternalValue();
      return false;
    }
    if (timeout_time != now_) {
      LOG(ERROR) << "Ignoring request to trigger timeout scheduled for "
                 << timeout_time.ToInternalValue() << " at "
                 << now_.ToInternalValue();
      return false;
    }
    test_api_.TriggerTimeout();
    return true;
  }

  // Advances |now_| by |interval| and calls TriggerTimeout().
  bool AdvanceTimeAndTriggerTimeout(base::TimeDelta interval) {
    AdvanceTime(interval);
    return TriggerTimeout();
  }

  // Advances |now_| by |next_delay| minus the last delay passed to this
  // method and calls TriggerTimeout().  This is useful when invoking
  // successive delays: for example, given delays at 2, 4, and 5 minutes,
  // instead of calling AdvanceTimeAndTriggerTimeout() with 2, (4 - 2), and
  // then (5 - 4), StepTimeAndTriggerTimeout() can be called with 2, 4, and
  // 5.  Call ResetLastStepDelay() before a new sequence of delays to reset
  // the "last delay".
  bool StepTimeAndTriggerTimeout(base::TimeDelta next_delay) {
    AdvanceTime(next_delay - last_step_delay_);
    last_step_delay_ = next_delay;
    return TriggerTimeout();
  }

  // Resets the "last delay" used by StepTimeAndTriggerTimeout().
  void ResetLastStepDelay() { last_step_delay_ = base::TimeDelta(); }

  FakePrefs prefs_;
  TestDelegate delegate_;
  StateController controller_;
  StateController::TestApi test_api_;

  base::TimeTicks now_;

  // Last delay that was passed to StepTimeAndTriggerTimeout().
  base::TimeDelta last_step_delay_;

  // Preference values.  Tests may change these before calling Init().
  base::TimeDelta default_ac_suspend_delay_;
  base::TimeDelta default_ac_screen_off_delay_;
  base::TimeDelta default_ac_screen_dim_delay_;
  base::TimeDelta default_battery_suspend_delay_;
  base::TimeDelta default_battery_screen_off_delay_;
  base::TimeDelta default_battery_screen_dim_delay_;
  base::TimeDelta default_screen_lock_delay_;
  int64 default_disable_idle_suspend_;
  int64 default_lock_on_idle_suspend_;
  int64 default_require_usb_input_device_to_suspend_;
  int64 default_keep_screen_on_for_audio_;

  // Values passed by Init() to StateController::Init().
  StateController::PowerSource initial_power_source_;
  StateController::LidState initial_lid_state_;
  StateController::SessionState initial_session_state_;
  StateController::DisplayMode initial_display_mode_;
};

// Tests the basic operation of the different delays.
TEST_F(StateControllerTest, BasicDelays) {
  Init();

  // The screen should be dimmed after the configured interval and then undimmed
  // in response to user activity.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());

  // The system should eventually suspend if the user is inactive.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // No further timeouts should be scheduled at this point.
  EXPECT_TRUE(test_api_.GetTimeoutTime().is_null());

  // When the system resumes, the screen should be undimmed and turned back
  // on.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // The screen should be dimmed again after the screen-dim delay.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that the screen isn't dimmed while video is detected.
TEST_F(StateControllerTest, VideoDefersDimming) {
  Init();

  // The screen shouldn't be dimmed while a video is playing.
  const base::TimeDelta kHalfDimDelay = default_ac_screen_dim_delay_ / 2;
  controller_.HandleVideoActivity();
  AdvanceTime(kHalfDimDelay);
  controller_.HandleVideoActivity();
  AdvanceTime(kHalfDimDelay);
  controller_.HandleVideoActivity();
  AdvanceTime(kHalfDimDelay);
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After the video stops, the dimming delay should happen as expected.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Video activity should undim the screen at this point.
  controller_.HandleVideoActivity();
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());

  // The dimming delay should fire again after the video stops.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that the screen dims, is turned off, and is locked while audio is
// playing
TEST_F(StateControllerTest, AudioDefersSuspend) {
  Init();

  // Report audio activity and check that the controller goes through the
  // usual dim->off->lock progression.
  controller_.HandleAudioActivity();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());

  // Report additional audio activity.  The controller should wait for the
  // full suspend delay before suspending.
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the system is suspended when the lid is closed.
TEST_F(StateControllerTest, LidCloseSuspendsByDefault) {
  Init();
  controller_.HandleLidStateChange(StateController::LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // After the lid is opened, the next delay should be screen-dimming (i.e.
  // all timers should be reset).
  controller_.HandleResume();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(StateController::LID_OPEN);
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that timeouts are reset when the user logs in or out.
TEST_F(StateControllerTest, SessionStateChangeResetsTimeouts) {
  Init();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // The screen should be undimmed and turned on when a user logs out.
  controller_.HandleSessionStateChange(StateController::SESSION_STOPPED);
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // The screen should be dimmed again after the usual delay.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests the controller shuts the system down instead of suspending when no
// user is logged in.
TEST_F(StateControllerTest, ShutDownWhenSessionStopped) {
  initial_session_state_ = StateController::SESSION_STOPPED;
  default_screen_lock_delay_ = base::TimeDelta();
  Init();

  // The screen should be dimmed and turned off, but the system should shut
  // down instead of suspending.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kShutDown, delegate_.GetActions());

  // Send a session-started notification (which is a bit unrealistic given
  // that the system was just shut down).
  controller_.HandleSessionStateChange(StateController::SESSION_STARTED);
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // The system should suspend now.
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());

  // After resuming and stopping the session, lid-close should shut the
  // system down.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandleSessionStateChange(StateController::SESSION_STOPPED);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(StateController::LID_CLOSED);
  EXPECT_EQ(kShutDown, delegate_.GetActions());
}

// Tests that the lock-on-suspend pref is honored and watched for changes.
TEST_F(StateControllerTest, LockPref) {
  // Disable the screen locking pref initially.
  default_lock_on_idle_suspend_ = false;
  Init();

  // Check that the screen is dimmed and turned off as expected.  The
  // system should be suspended instead of getting locked after this.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // The screen should be turned on and undimmed in response to user activity.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // Set the lock-on-suspend pref and notify the controller that it changed.
  ASSERT_TRUE(prefs_.SetInt64(kLockOnIdleSuspendPref, 1));
  prefs_.NotifyObservers(kLockOnIdleSuspendPref);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // The screen should be locked and then suspended after being dimmed and
  // turned off now.
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that delays are scaled while presenting and that they return to
// their original values when not presenting.
TEST_F(StateControllerTest, ScaleDelaysWhilePresenting) {
  initial_display_mode_ = StateController::DISPLAY_PRESENTATION;
  Init();

  // The suspend delay should be scaled; all others should be updated to
  // retain the same difference from the suspend delay as before.
  base::TimeDelta suspend_delay = default_ac_suspend_delay_ *
      StateController::kDefaultPresentationIdleDelayFactor;
  base::TimeDelta screen_lock_delay = suspend_delay -
      (default_ac_suspend_delay_ - default_screen_lock_delay_);
  base::TimeDelta screen_off_delay = suspend_delay -
      (default_ac_suspend_delay_ - default_ac_screen_off_delay_);
  base::TimeDelta screen_dim_delay = suspend_delay -
      (default_ac_suspend_delay_ - default_ac_screen_dim_delay_);

  ASSERT_TRUE(StepTimeAndTriggerTimeout(screen_dim_delay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(screen_off_delay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(screen_lock_delay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(suspend_delay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandleDisplayModeChange(StateController::DISPLAY_NORMAL);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the appropriate delays are used when switching between battery
// and AC power.
TEST_F(StateControllerTest, PowerSourceChange) {
  // Start out on battery power.
  initial_power_source_ = StateController::POWER_BATTERY;
  default_battery_screen_dim_delay_ = base::TimeDelta::FromSeconds(60);
  default_battery_screen_off_delay_ = base::TimeDelta::FromSeconds(90);
  default_battery_suspend_delay_ = base::TimeDelta::FromSeconds(100);
  default_ac_screen_dim_delay_ = base::TimeDelta::FromSeconds(120);
  default_ac_screen_off_delay_ = base::TimeDelta::FromSeconds(150);
  default_ac_suspend_delay_ = base::TimeDelta::FromSeconds(160);
  default_screen_lock_delay_ = base::TimeDelta::FromSeconds(155);
  Init();

  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  // Since there's only one lock-delay pref for both battery and AC, and it
  // exceeds the battery suspend delay, the controller should skip locking
  // the screen.  (If the user has set the lock-on-suspend pref, Chrome
  // will still lock the screen before the system suspends -- only the
  // timed screen-lock is skipped here.)
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Switch to AC power and check that the AC delays are used instead.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandlePowerSourceChange(StateController::POWER_AC);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Resume and wait for the screen to be dimmed.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Switch back to battery.  The controller should treat the power source
  // change as a user action and undim the screen (rather than e.g.
  // suspending immediately since |default_battery_suspend_delay_| has been
  // exceeded) and then proceed through the battery delays.
  controller_.HandlePowerSourceChange(StateController::POWER_BATTERY);
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that externally-supplied policy supercedes powerd's default prefs.
TEST_F(StateControllerTest, PolicySupercedesPrefs) {
  Init();

  // Set an external policy that disables most delays and instructs the
  // power manager to log the user out after 10 minutes of inactivity.
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(600);
  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(0);
  policy.mutable_ac_delays()->set_screen_dim_ms(0);
  policy.mutable_ac_delays()->set_screen_lock_ms(0);
  *policy.mutable_battery_delays() = policy.ac_delays();
  policy.set_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  policy.set_lid_closed_action(PowerManagementPolicy_Action_DO_NOTHING);
  policy.set_use_audio_activity(false);
  policy.set_use_video_activity(false);
  policy.set_presentation_idle_delay_factor(1.0);
  controller_.HandlePolicyChange(policy);

  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  controller_.HandleUserActivity();
  controller_.HandleDisplayModeChange(StateController::DISPLAY_PRESENTATION);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Wait for half of the idle delay and then report user activity, which
  // should reset the logout timeout.  Audio and video activity should not
  // reset the timeout, however.
  AdvanceTime(kIdleDelay / 2);
  controller_.HandleUserActivity();
  AdvanceTime(kIdleDelay / 2);
  controller_.HandleAudioActivity();
  controller_.HandleVideoActivity();
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay / 2));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // The policy's request to do nothing when the lid is closed should be
  // honored.
  controller_.HandleLidStateChange(StateController::LID_CLOSED);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Wait 120 seconds and then send an updated policy that dims the screen
  // after 60 seconds.  The screen should dim immediately.
  AdvanceTime(base::TimeDelta::FromSeconds(120));
  policy.mutable_ac_delays()->set_screen_dim_ms(60000);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Switch to battery power, which still has an unset screen-dimming
  // delay.  The screen should undim immediately.
  controller_.HandlePowerSourceChange(StateController::POWER_BATTERY);
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());

  // Update the policy again to shut down if the lid is closed.  Since the
  // lid is already closed, the system should shut down immediately.
  policy.set_lid_closed_action(PowerManagementPolicy_Action_SHUT_DOWN);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kShutDown, delegate_.GetActions());
}

// Test that unset fields in a policy are ignored.
TEST_F(StateControllerTest, PartiallyFilledPolicy) {
  Init();

  // Set a policy that has a very short dimming delay but leaves all other
  // fields unset.
  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(1);
  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);

  // The policy's dimming delay should be used, but the rest of the delays
  // should come from prefs.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // Setting an empty policy should revert to the values from the prefs.
  policy.Clear();
  controller_.HandlePolicyChange(policy);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that policies that enable audio detection while disabling video
// detection result in the screen getting locked at the expected time but
// defer suspend.
TEST_F(StateControllerTest, PolicyDisablingVideo) {
  Init();

  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(300);
  const base::TimeDelta kOffDelay = base::TimeDelta::FromSeconds(310);
  const base::TimeDelta kLockDelay = base::TimeDelta::FromSeconds(320);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(330);

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(kOffDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_lock_ms(kLockDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.set_idle_action(PowerManagementPolicy_Action_SUSPEND);
  policy.set_use_audio_activity(true);
  policy.set_use_video_activity(false);
  controller_.HandlePolicyChange(policy);

  // Proceed through the screen-dim, screen-off, and screen-lock delays,
  // reporting video and audio activity along the way.  The screen should
  // be locked (since |use_video_activity| is false).
  controller_.HandleVideoActivity();
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  controller_.HandleVideoActivity();
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  controller_.HandleVideoActivity();
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kLockDelay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());

  // The system shouldn't suspend until a full |kIdleDelay| after the last
  // report of audio activity, since |use_audio_activity| is false.
  controller_.HandleVideoActivity();
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the controller does something reasonable if the lid is closed
// just as the idle delay is reached but before the timeout has fired.
TEST_F(StateControllerTest, SimultaneousIdleAndLidActions) {
  Init();

  // Step through the normal delays.  Just when the suspend delay is about
  // to run, close the lid.  We should only make one suspend attempt.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kScreenLock, NULL),
            delegate_.GetActions());
  AdvanceTime(default_ac_suspend_delay_ - default_screen_lock_delay_);
  controller_.HandleLidStateChange(StateController::LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the screen stays on while audio is playing if
// |kKeepBacklightOnForAudioPref| is set.
TEST_F(StateControllerTest, KeepScreenOnForAudio) {
  default_keep_screen_on_for_audio_ = 1;
  Init();
  const base::TimeDelta kHalfScreenOffDelay = default_ac_screen_off_delay_ / 2;

  // The screen should be dimmed as usual.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // After audio is reported, screen-off should be deferred.  The next action
  // should instead be locking the screen.
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());

  // Continue reporting audio activity; the screen should stay on.
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  AdvanceTime(kHalfScreenOffDelay);
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  AdvanceTime(kHalfScreenOffDelay);
  controller_.HandleAudioActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After the audio activity stops, the screen should be turned off after
  // the normal screen-off delay.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());

  // Audio activity should turn the screen back on.
  controller_.HandleAudioActivity();
  EXPECT_EQ(kScreenOn, delegate_.GetActions());

  // Turn the screen off again and check that the next action is suspending.
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the |kRequireUsbInputDeviceToSuspendPref| pref is honored.
TEST_F(StateControllerTest, RequireUsbInputDeviceToSuspend) {
  default_require_usb_input_device_to_suspend_ = 1;
  delegate_.set_usb_input_device_connected(false);
  Init();

  // Advance through the usual delays.  The suspend timeout should
  // trigger as before, but no action should be performed.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After a USB input device is connected, the system should suspend as
  // before.
  delegate_.set_usb_input_device_connected(true);
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that suspend is deferred before OOBE is completed.
TEST_F(StateControllerTest, DontSuspendBeforeOobeCompleted) {
  delegate_.set_oobe_completed(false);
  default_screen_lock_delay_ = base::TimeDelta();
  Init();

  // The screen should dim and turn off as usual, but the system shouldn't
  // be suspended.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Report user activity and mark OOBE as done.  The system should suspend
  // this time.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  delegate_.set_oobe_completed(true);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the disable-idle-suspend pref is honored and overrides policies.
TEST_F(StateControllerTest, DisableIdleSuspend) {
  default_disable_idle_suspend_ = 1;
  Init();

  // With the disable-idle-suspend pref set, the system shouldn't suspend
  // when it's idle.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kScreenLock, NULL),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Even after explicitly setting a policy to suspend on idle, the system
  // should still stay up.
  PowerManagementPolicy policy;
  policy.set_idle_action(PowerManagementPolicy_Action_SUSPEND);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
}

// Tests that state overrides are honored.
TEST_F(StateControllerTest, Overrides) {
  Init();

  // Override everything.  The idle timeout should fire but do nothing.
  controller_.HandleOverrideChange(true /* override_screen_dim */,
                                   true /* override_screen_off */,
                                   true /* override_idle_suspend */,
                                   true /* override_lid_suspend */);
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(StateController::LID_CLOSED);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(StateController::LID_OPEN);

  // Override the suspend properties but not the screen-related delays and
  // check that the controller dims, turns off, and locks the screen but
  // doesn't suspend the system.
  controller_.HandleOverrideChange(false /* override_screen_dim */,
                                   false /* override_screen_off */,
                                   true /* override_idle_suspend */,
                                   true /* override_lid_suspend */);
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_screen_lock_delay_));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(StateController::LID_CLOSED);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // If the lid override is removed while the lid is still closed, the
  // system should suspend immediately.
  controller_.HandleOverrideChange(false /* override_screen_dim */,
                                   false /* override_screen_off */,
                                   true /* override_idle_suspend */,
                                   false /* override_lid_suspend */);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the controller does something reasonable when given delays
// that don't make sense.
TEST_F(StateControllerTest, InvalidDelays) {
  // The dim delay should be less than the off delay, which should be less
  // than the lock delay, which should be less than the idle delay.  All of
  // those constraints are violated here, so all of the other delays should
  // be capped to the idle delay (except for the lock delay, which is
  // disabled in favor of Chrome just locking before the system suspends).
  default_ac_screen_dim_delay_ = base::TimeDelta::FromSeconds(120);
  default_ac_screen_off_delay_ = base::TimeDelta::FromSeconds(110);
  default_screen_lock_delay_ = base::TimeDelta::FromSeconds(100);
  default_ac_suspend_delay_ = base::TimeDelta::FromSeconds(90);
  Init();
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());
}

}  // namespace policy
}  // namespace power_manager
