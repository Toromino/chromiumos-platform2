// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_
#define DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/optional.h>
#include <base/time/default_tick_clock.h>
#include <base/time/tick_clock.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

namespace diagnostics {

// This class is responsible for providing the actions corresponding to the
// command-line arguments for the diag tool. Only capable of running a single
// routine at a time.
class DiagActions final {
 public:
  // The two TimeDelta inputs are used to configure this instance's polling
  // behavior - the time between polls, and the maximum time before giving up on
  // a running routine.
  // Override |tick_clock| for testing only.
  DiagActions(base::TimeDelta polling_interval,
              base::TimeDelta maximum_execution_time,
              const base::TickClock* tick_clock = nullptr);
  DiagActions(const DiagActions&) = delete;
  DiagActions& operator=(const DiagActions&) = delete;
  ~DiagActions();

  // Print a list of routines available on the platform. Returns true iff all
  // available routines were successfully converted to human-readable strings
  // and printed.
  bool ActionGetRoutines();
  // Run a particular diagnostic routine. See mojo/cros_healthd.mojom for
  // details on the individual routines. Returns true iff the routine completed.
  // Note that this does not mean the routine succeeded, only that it started,
  // ran, and was removed.
  bool ActionRunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type);
  bool ActionRunBatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah);
  bool ActionRunBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed);
  bool ActionRunBatteryHealthRoutine(uint32_t maximum_cycle_count,
                                     uint32_t percent_battery_wear_allowed);
  bool ActionRunCpuCacheRoutine(base::TimeDelta exec_duration);
  bool ActionRunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb);
  bool ActionRunCpuStressRoutine(base::TimeDelta exec_duration);
  bool ActionRunFloatingPointAccuracyRoutine(base::TimeDelta exec_duration);
  bool ActionRunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type);
  bool ActionRunNvmeWearLevelRoutine(uint32_t wear_level_threshold);
  bool ActionRunPrimeSearchRoutine(base::TimeDelta exec_duration,
                                   uint64_t max_num);
  bool ActionRunSmartctlCheckRoutine();
  bool ActionRunUrandomRoutine(uint32_t length_seconds);

  // Cancels the next routine run, when that routine reports a progress percent
  // greater than or equal to |percent|. Should be called before running the
  // routine to be cancelled.
  void ForceCancelAtPercent(uint32_t percent);

 private:
  // Helper function to determine when a routine has finished. Also does any
  // necessary cleanup.
  bool PollRoutineAndProcessResult();
  // Displays the user message from |interactive_result|, then blocks for user
  // input. After receiving input, resets the polling time and continues to
  // poll.
  bool ProcessInteractiveResultAndContinue(
      chromeos::cros_healthd::mojom::InteractiveRoutineUpdatePtr
          interactive_result);
  // Displays information from a noninteractive routine update and removes the
  // routine corresponding to |id_|.
  bool ProcessNonInteractiveResultAndEnd(
      chromeos::cros_healthd::mojom::NonInteractiveRoutineUpdatePtr
          noninteractive_result);
  // Attempts to remove the routine corresponding to |id_|.
  void RemoveRoutine();

  // Used to send mojo requests to cros_healthd.
  CrosHealthdMojoAdapter adapter_;
  // ID of the routine being run.
  int32_t id_ = chromeos::cros_healthd::mojom::kFailedToStartId;

  // If |force_cancel_| is true, the next routine run will be cancelled when its
  // progress is greater than or equal to |cancellation_percent_|.
  bool force_cancel_ = false;
  uint32_t cancellation_percent_ = 0;

  // Polling interval.
  const base::TimeDelta kPollingInterval;
  // Maximum time we're willing to wait for a routine to finish.
  const base::TimeDelta kMaximumExecutionTime;

  // Tracks the passage of time.
  std::unique_ptr<base::DefaultTickClock> default_tick_clock_;
  // Unowned pointer which should outlive this instance. Allows the default tick
  // clock to be overridden for testing.
  const base::TickClock* tick_clock_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_
