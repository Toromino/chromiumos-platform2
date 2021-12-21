// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/components_repair_state_handler.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rmad/constants.h"
#include "rmad/system/fake_runtime_probe_client.h"
#include "rmad/system/runtime_probe_client_impl.h"
#include "rmad/utils/dbus_utils.h"

#include <base/logging.h>

namespace rmad {

using ComponentRepairStatus = ComponentsRepairState::ComponentRepairStatus;
using RepairStatus = ComponentRepairStatus::RepairStatus;

namespace {

const std::unordered_set<RmadComponent> kProbeableComponents = {
    RMAD_COMPONENT_BATTERY,  RMAD_COMPONENT_STORAGE,
    RMAD_COMPONENT_CAMERA,   RMAD_COMPONENT_STYLUS,
    RMAD_COMPONENT_TOUCHPAD, RMAD_COMPONENT_TOUCHSCREEN,
    RMAD_COMPONENT_DRAM,     RMAD_COMPONENT_DISPLAY_PANEL,
    RMAD_COMPONENT_CELLULAR, RMAD_COMPONENT_ETHERNET,
    RMAD_COMPONENT_WIRELESS,
};

const std::unordered_set<RmadComponent> kUnprobeableComponents = {
    RMAD_COMPONENT_KEYBOARD,           RMAD_COMPONENT_POWER_BUTTON,
    RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
    RMAD_COMPONENT_BASE_GYROSCOPE,     RMAD_COMPONENT_LID_GYROSCOPE,
    RMAD_COMPONENT_AUDIO_CODEC,
};

// Convert the list of |ComponentRepairStatus| in |state| to a mapping table of
// component repair states. Unfortunately protobuf doesn't support enum as map
// keys so we can only store them in a list in protobuf and convert to a map
// internally.
std::unordered_map<RmadComponent, RepairStatus> ConvertStateToDictionary(
    const RmadState& state) {
  std::unordered_map<RmadComponent, RepairStatus> component_status_map;
  if (state.has_components_repair()) {
    const ComponentsRepairState& components_repair = state.components_repair();
    for (int i = 0; i < components_repair.components_size(); ++i) {
      const ComponentRepairStatus& components = components_repair.components(i);
      const RmadComponent& component = components.component();
      const RepairStatus& repair_status = components.repair_status();
      if (component == RMAD_COMPONENT_UNKNOWN) {
        LOG(WARNING) << "RmadState component missing |component| field.";
        continue;
      }
      if (component_status_map.count(component) > 0) {
        LOG(WARNING) << "RmadState has duplicate components "
                     << RmadComponent_Name(component);
        continue;
      }
      component_status_map.insert({component, repair_status});
    }
  }
  return component_status_map;
}

// Convert a dictionary of {RmadComponent: RepairStatus} to a |RmadState|.
RmadState ConvertDictionaryToState(
    const std::unordered_map<RmadComponent, RepairStatus>& component_status_map,
    bool mainboard_rework) {
  auto components_repair = std::make_unique<ComponentsRepairState>();
  for (auto [component, repair_status] : component_status_map) {
    if (component == RMAD_COMPONENT_UNKNOWN) {
      LOG(WARNING) << "Dictionary contains UNKNOWN component";
      continue;
    }
    ComponentRepairStatus* components = components_repair->add_components();
    components->set_component(component);
    components->set_repair_status(repair_status);
  }
  components_repair->set_mainboard_rework(mainboard_rework);

  RmadState state;
  state.set_allocated_components_repair(components_repair.release());
  return state;
}

}  // namespace

namespace fake {

FakeComponentsRepairStateHandler::FakeComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : ComponentsRepairStateHandler(
          json_store, std::make_unique<FakeRuntimeProbeClient>()) {}

}  // namespace fake

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store), active_(false) {
  runtime_probe_client_ =
      std::make_unique<RuntimeProbeClientImpl>(GetSystemBus());
}

ComponentsRepairStateHandler::ComponentsRepairStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<RuntimeProbeClient> runtime_probe_client)
    : BaseStateHandler(json_store),
      active_(false),
      runtime_probe_client_(std::move(runtime_probe_client)) {}

RmadErrorCode ComponentsRepairStateHandler::InitializeState() {
  // Probing takes a lot of time. Early return to avoid probing again if we are
  // already in this state.
  if (active_) {
    return RMAD_ERROR_OK;
  }

  if (!state_.has_components_repair() && !RetrieveState()) {
    state_.set_allocated_components_repair(new ComponentsRepairState);
  }

  // Initialize probeable and unprobeable components. Ideally |state_| always
  // contain the full list of components, unless it's just being created, but
  // it's possible that the state file on the device is old and the latest image
  // introduces some new components. This could happen on stocked mainboards.
  // We also assume we don't remove any component enums for backward
  // compatibility.
  std::unordered_map<RmadComponent, RepairStatus> component_status_map =
      ConvertStateToDictionary(state_);
  for (RmadComponent component : kProbeableComponents) {
    if (component_status_map.count(component) == 0) {
      component_status_map[component] =
          ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING;
    }
  }
  for (RmadComponent component : kUnprobeableComponents) {
    if (component_status_map.count(component) == 0) {
      component_status_map[component] =
          ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN;
    }
  }

  // Call runtime_probe to get all probed components.
  ComponentsWithIdentifier probed_components;
  if (!runtime_probe_client_->ProbeCategories({}, &probed_components)) {
    LOG(ERROR) << "Failed to get probe result from runtime_probe";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  // Update probeable components using runtime_probe results.
  // 1. If a probed component is marked MISSING, the component was not probed
  //    previously, or this is the first probe. Mark the component as UNKNOWN.
  // 2. If a component is not marked MISSING, but it doesn't appear in the
  //    latest probed result, mark the component as MISSING.
  // TODO(chenghan): Use the identifier provided by runtime_probe.
  std::set<RmadComponent> probed_component_set;
  for (const auto& [component, unused_identidier] : probed_components) {
    probed_component_set.insert(component);
    CHECK_GT(kProbeableComponents.count(component), 0);
    if (component_status_map[component] ==
        ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
      component_status_map[component] =
          ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN;
    }
  }
  for (RmadComponent component : kProbeableComponents) {
    if (probed_component_set.count(component) == 0) {
      component_status_map[component] =
          ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING;
    }
  }

  state_ = ConvertDictionaryToState(
      component_status_map, state_.components_repair().mainboard_rework());
  active_ = true;
  return RMAD_ERROR_OK;
}

void ComponentsRepairStateHandler::CleanUpState() {
  active_ = false;
}

BaseStateHandler::GetNextStateCaseReply
ComponentsRepairStateHandler::GetNextStateCase(const RmadState& state) {
  if (!ApplyUserSelection(state)) {
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  // Store the state to storage to keep user's selection.
  StoreState();
  StoreVars();

  return NextStateCaseWrapper(RmadState::StateCase::kDeviceDestination);
}

bool ComponentsRepairStateHandler::ApplyUserSelection(const RmadState& state) {
  if (!state.has_components_repair()) {
    LOG(ERROR) << "RmadState missing |components repair| state.";
    return false;
  }

  std::unordered_map<RmadComponent, RepairStatus> current_map =
      ConvertStateToDictionary(state_);
  const std::unordered_map<RmadComponent, RepairStatus> update_map =
      ConvertStateToDictionary(state);
  const bool mainboard_rework = state.components_repair().mainboard_rework();

  if (mainboard_rework) {
    // MLB rework. Set all the probed components to REPLACED.
    for (auto& [component, repair_status] : current_map) {
      if (repair_status != ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
        repair_status = ComponentRepairStatus::RMAD_REPAIR_STATUS_REPLACED;
      }
    }
  } else {
    // Not MLB rework. Use |update_map| to update |current_map|.
    for (auto [component, repair_status] : update_map) {
      const std::string component_name = RmadComponent_Name(component);
      if (current_map.count(component) == 0) {
        LOG(ERROR) << "New state contains an unknown component "
                   << component_name;
        return false;
      }
      RepairStatus prev_repair_status = current_map[component];
      if (prev_repair_status ==
              ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING &&
          repair_status != ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
        LOG(ERROR) << "New state contains repair state for unprobed component "
                   << component_name;
        return false;
      }
      if (prev_repair_status !=
              ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING &&
          repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING) {
        LOG(ERROR) << "New state missing repair state for component "
                   << component_name;
        return false;
      }
      current_map[component] = repair_status;
    }
  }
  // Check if there are any components that still has UNKNOWN repair state.
  for (auto [component, repair_status] : current_map) {
    if (repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_UNKNOWN) {
      LOG(ERROR) << "Component " << RmadComponent_Name(component)
                 << " has unknown repair state";
      return false;
    }
  }

  // Convert |current_map| back to |state_|.
  state_ = ConvertDictionaryToState(current_map, mainboard_rework);
  return true;
}

bool ComponentsRepairStateHandler::StoreVars() const {
  std::vector<std::string> replaced_components;
  const std::unordered_map<RmadComponent, RepairStatus> component_status_map =
      ConvertStateToDictionary(state_);

  for (auto [component, repair_status] : component_status_map) {
    if (repair_status == ComponentRepairStatus::RMAD_REPAIR_STATUS_REPLACED) {
      replaced_components.push_back(RmadComponent_Name(component));
    }
  }

  bool mlb_repair = state_.components_repair().mainboard_rework();
  return json_store_->SetValue(kReplacedComponentNames, replaced_components) &&
         json_store_->SetValue(kMlbRepair, mlb_repair);
}

}  // namespace rmad
