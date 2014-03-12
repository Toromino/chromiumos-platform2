// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_DARK_RESUME_POLICY_H_
#define POWER_MANAGER_POWERD_DARK_RESUME_POLICY_H_

#include <map>
#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/files/file_path.h>

#include "power_manager/powerd/system/power_supply.h"

namespace power_manager {

class PrefsInterface;

namespace policy {

class DarkResumePolicy {
 public:
  enum Action {
    // Suspend the system and resume after a set duration.
    SUSPEND_FOR_DURATION,
    // Shut the system down immediately.
    SHUT_DOWN,
    // Do a normal suspend without setting an alarm to wakeup later.
    SUSPEND_INDEFINITELY
  };

  DarkResumePolicy();
  ~DarkResumePolicy();

  // Reads preferences on how long to suspend, what devices are affected by
  // suspend, and what devices can wake the system up from suspend.
  // Ownership of passed-in pointers remains with the caller.
  void Init(system::PowerSupply* power_supply, PrefsInterface* prefs);

  // Returns an enum that indicates what action should be taken.
  Action GetAction();

  // Returns how long the system should suspend. This is based on the charge of
  // the battery for now. This should be called immediately after GetAction() if
  // it returns SUSPEND_FOR_DURATION.
  base::TimeDelta GetSuspendDuration();

  // Checks if the system is in the dark resume state.
  bool CurrentlyInDarkResume();

  // Cleans up its internal state after a user initiated resume happens.
  void HandleResume();

 private:
  // Reads a string pref named |pref_name| from |prefs_| and splits it on
  // newlines into |lines|. Returns false if the pref doesn't exist.
  bool ExtractLines(const std::string& pref_name,
                    std::vector<std::string>* lines);

  bool ReadSuspendDurationsPref();

  bool ReadBatteryMarginsPref();

  // This enables functionality for dark resume in devices that are listed in
  // the prefs_file. The base_file is the name of the sysfs file we are writing
  // to to enable the functionality for dark resume. This includes whether the
  // device should do something different during a dark resume or whether it is
  // a wakeup source for dark resume.
  void GetFiles(std::vector<base::FilePath>* files,
                const std::string& pref_name,
                const std::string& base_file);

  void SetStates(const std::vector<base::FilePath>& files,
                 const std::string& state);

  // Updates |battery_shutdown_threshold_|, |battery_suspend_level_|, and
  // |thresholds_set_|.
  void SetThresholds();

  bool enabled_;

  system::PowerSupply* power_supply_;
  PrefsInterface* prefs_;

  system::PowerStatus power_status_;

  // Battery threshold which we use to tell if we should shut down after a dark
  // resume. This is set at the last suspend that was not from a dark resume.
  // Read from prefs.
  double battery_shutdown_threshold_;

  // The battery level from when the machine suspended. If we wake up and the
  // battery level is higher than when we suspended, this and the shut down
  // threshold are changed.
  double battery_suspend_level_;

  bool thresholds_set_;

  // How much the battery should go down before we shut down the computer. Read
  // from prefs.
  typedef std::map<double, double> MarginMap;
  MarginMap battery_margins_;

  // A map of battery charges mapped to suspend durations (in seconds). The
  // system uses the suspend time (unsigned int) association with the highest
  // battery charge that it is greater than or equal to.
  typedef std::map<double, base::TimeDelta> SuspendMap;
  SuspendMap suspend_durations_;

  std::vector<base::FilePath> dark_resume_sources_;
  std::vector<base::FilePath> dark_resume_devices_;

  DISALLOW_COPY_AND_ASSIGN(DarkResumePolicy);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_DARK_POLICY_H_
