// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_ENABLE_PHYSICAL_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_ENABLE_PHYSICAL_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/timer/timer.h>

namespace rmad {

class CrosSystemUtils;

class WriteProtectEnablePhysicalStateHandler : public BaseStateHandler {
 public:
  // Poll every 2 seconds.
  static constexpr base::TimeDelta kPollInterval = base::Seconds(2);

  explicit WriteProtectEnablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store);
  // Used to inject mock |crossystem_utils_| for testing.
  WriteProtectEnablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<CrosSystemUtils> crossystem_utils);

  void RegisterSignalSender(
      base::RepeatingCallback<void(bool)> callback) override {
    write_protect_signal_sender_ = callback;
  }

  ASSIGN_STATE(RmadState::StateCase::kWpEnablePhysical);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~WriteProtectEnablePhysicalStateHandler() override = default;

 private:
  void PollUntilWriteProtectOn();
  void CheckWriteProtectOnTask();

  base::RepeatingTimer timer_;
  base::RepeatingCallback<void(bool)> write_protect_signal_sender_;

  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
};

namespace fake {

class FakeWriteProtectEnablePhysicalStateHandler
    : public WriteProtectEnablePhysicalStateHandler {
 public:
  FakeWriteProtectEnablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store,
      const base::FilePath& working_dir_path);

 protected:
  ~FakeWriteProtectEnablePhysicalStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_ENABLE_PHYSICAL_STATE_HANDLER_H_
