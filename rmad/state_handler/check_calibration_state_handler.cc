// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <limits>
#include <memory>
#include <string>

#include <base/logging.h>

namespace rmad {

CheckCalibrationStateHandler::CheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

RmadErrorCode CheckCalibrationStateHandler::InitializeState() {
  if (!state_.has_check_calibration()) {
    state_.set_allocated_check_calibration(new CheckCalibrationState);
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
CheckCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  bool need_calibration;
  RmadErrorCode error_code;
  if (!CheckIsCalibrationRequired(state, &need_calibration, &error_code)) {
    return {.error = error_code, .state_case = GetStateCase()};
  }

  state_ = state;
  StoreVars();

  if (need_calibration) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kSetupCalibration};
  }

  return {.error = RMAD_ERROR_OK,
          .state_case = RmadState::StateCase::kProvisionDevice};
}

bool CheckCalibrationStateHandler::CheckIsCalibrationRequired(
    const RmadState& state, bool* need_calibration, RmadErrorCode* error_code) {
  if (!state.has_check_calibration()) {
    LOG(ERROR) << "RmadState missing |components calibrate| state.";
    *error_code = RMAD_ERROR_REQUEST_INVALID;
    return false;
  }

  *need_calibration = false;

  const CheckCalibrationState& check_calibration = state.check_calibration();
  for (int i = 0; i < check_calibration.components_size(); ++i) {
    const CalibrationComponentStatus& component_status =
        check_calibration.components(i);
    if (component_status.component() == RmadComponent::RMAD_COMPONENT_UNKNOWN) {
      *error_code = RMAD_ERROR_REQUEST_ARGS_MISSING;
      LOG(ERROR) << "RmadState missing |component| argument.";
      return false;
    }

    CalibrationSetupInstruction instruction =
        GetCalibrationSetupInstruction(component_status.component());
    if (instruction == RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {
      *error_code = RMAD_ERROR_CALIBRATION_COMPONENT_INVALID;
      LOG_STREAM(ERROR) << RmadComponent_Name(component_status.component())
                        << " cannot be calibrated.";
      return false;
    }

    // Since the entire calibration process is check -> setup -> calibrate ->
    // complete or return to check, the status may be waiting, in progress
    // (timeout), failed, complete or skip here.
    switch (component_status.status()) {
      // For in progress and failed cases, we also need to calibrate it.
      case CalibrationComponentStatus::RMAD_CALIBRATION_WAITING:
      case CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS:
      case CalibrationComponentStatus::RMAD_CALIBRATION_FAILED:
        *need_calibration = true;
        break;
      // For those already calibrated and skipped component, we don't need to
      // calibrate it.
      case CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE:
      case CalibrationComponentStatus::RMAD_CALIBRATION_SKIP:
        break;
      case CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN:
      default:
        *error_code = RMAD_ERROR_REQUEST_ARGS_MISSING;
        LOG(ERROR)
            << "RmadState component missing |calibration_status| argument.";
        return false;
    }

    setup_instruction_calibration_map_[instruction]
                                      [component_status.component()] =
                                          component_status.status();
  }

  *error_code = RMAD_ERROR_OK;
  return true;
}

bool CheckCalibrationStateHandler::StoreVars() const {
  // In order to save dictionary style variables to json, currently only
  // variables whose keys are strings are supported. This is why we converted
  // it to a string. In addition, in order to ensure that the file is still
  // readable after the enum sequence is updated, we also convert its value
  // into a readable string to deal with possible updates.
  std::map<std::string, std::map<std::string, std::string>> json_value_map;
  for (auto setup_instruction_components : setup_instruction_calibration_map_) {
    std::string instruction =
        CalibrationSetupInstruction_Name(setup_instruction_components.first);
    for (auto component_status : setup_instruction_components.second) {
      std::string component_name = RmadComponent_Name(component_status.first);
      std::string status_name =
          CalibrationComponentStatus::CalibrationStatus_Name(
              component_status.second);
      json_value_map[instruction][component_name] = status_name;
    }
  }

  return json_store_->SetValue(kCalibrationMap, json_value_map);
}

}  // namespace rmad