// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

class WriteProtectDisableCompleteStateHandler : public BaseStateHandler {
 public:
  explicit WriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store);
  ~WriteProtectDisableCompleteStateHandler() override = default;

  ASSIGN_STATE(RmadState::StateCase::kWpDisableComplete);

  RmadState::StateCase GetNextStateCase() const override;
  RmadErrorCode UpdateState(const RmadState& state) override;
  RmadErrorCode ResetState() override;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_