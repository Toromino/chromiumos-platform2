// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_COMMON_POWER_CONSTANTS_H_
#define POWER_MANAGER_COMMON_POWER_CONSTANTS_H_

#include <base/basictypes.h>

namespace power_manager {

// Preference names.
extern const char kPluggedBrightnessOffsetPref[];
extern const char kUnpluggedBrightnessOffsetPref[];
extern const char kLowBatteryShutdownTimePref[];
extern const char kLowBatteryShutdownPercentPref[];
extern const char kCleanShutdownTimeoutMsPref[];
extern const char kPluggedDimMsPref[];
extern const char kPluggedOffMsPref[];
extern const char kUnpluggedDimMsPref[];
extern const char kUnpluggedOffMsPref[];
extern const char kUnpluggedSuspendMsPref[];
extern const char kEnforceLockPref[];
extern const char kDisableIdleSuspendPref[];
extern const char kUseLidPref[];
extern const char kLockOnIdleSuspendPref[];
extern const char kLockMsPref[];
extern const char kRetrySuspendMsPref[];
extern const char kRetrySuspendAttemptsPref[];
extern const char kPluggedSuspendMsPref[];
extern const char kMinVisibleBacklightLevelPref[];
extern const char kInstantTransitionsBelowMinLevelPref[];
extern const char kDisableALSPref[];
extern const char kKeepBacklightOnForAudioPref[];
extern const char kWakeupInputPref[];
extern const char kReactMsPref[];
extern const char kFuzzMsPref[];
extern const char kStateMaxDisabledDurationSecPref[];
extern const char kSampleWindowMaxPref[];
extern const char kSampleWindowMinPref[];
extern const char kTaperTimeMaxPref[];
extern const char kTaperTimeMinPref[];
extern const char kPowerSupplyFullFactorPref[];
extern const char kKeyboardBacklightAlsLimitsPref[];
extern const char kKeyboardBacklightAlsStepsPref[];
extern const char kKeyboardBacklightUserLimitsPref[];
extern const char kKeyboardBacklightUserStepsPref[];
extern const char kRequireUsbInputDeviceToSuspendPref[];
extern const char kBatteryPollIntervalPref[];
extern const char kBatteryPollShortIntervalPref[];
extern const char kTurnOffScreenTimeoutMsPref[];

// Miscellaneous constants.
extern const char kInternalBacklightPath[];
extern const char kInternalBacklightPattern[];
extern const char kKeyboardBacklightPath[];
extern const char kKeyboardBacklightPattern[];
extern const int64 kBatteryPercentPinMs;
extern const int64 kBatteryPercentTaperMs;

// Broadcast signals.
extern const char kPowerStateChanged[];

// Files to signal powerd_suspend whether suspend should be cancelled.
extern const char kLidOpenFile[];
extern const char kUserActiveFile[];

// Reasons for shutting down
extern const char kShutdownReasonUnknown[];
extern const char kShutdownReasonUserRequest[];
extern const char kShutdownReasonLidClosed[];
extern const char kShutdownReasonIdle[];
extern const char kShutdownReasonLowBattery[];
extern const char kShutdownReasonSuspendFailed[];

// Sources of input events.
enum InputType {
  INPUT_LID,
  INPUT_POWER_BUTTON,
  INPUT_UNHANDLED,
};

}  // namespace power_manager

#endif  // POWER_MANAGER_COMMON_POWER_CONSTANTS_H_
