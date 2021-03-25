// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/rmad_interface_impl.h"

#include <memory>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/values.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

const base::FilePath kDefaultJsonStoreFilePath("/var/lib/rmad/state");
constexpr char kRmadCurrentState[] = "current_state";

namespace {

bool RoVerificationKeyPressed() {
  // TODO(b/181000999): Send a D-Bus query to tpm_managerd when API is ready.
  return false;
}

}  // namespace

RmadInterfaceImpl::RmadInterfaceImpl() : RmadInterface() {
  json_store_ = base::MakeRefCounted<JsonStore>(kDefaultJsonStoreFilePath);
  state_handler_manager_ = std::make_unique<StateHandlerManager>(json_store_);
  state_handler_manager_->InitializeStateHandlers();
  InitializeState();
}

RmadInterfaceImpl::RmadInterfaceImpl(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<StateHandlerManager> state_handler_manager)
    : RmadInterface(),
      json_store_(json_store),
      state_handler_manager_(std::move(state_handler_manager)) {
  InitializeState();
}

void RmadInterfaceImpl::InitializeState() {
  if (const base::Value * value;
      json_store_->GetValue(kRmadCurrentState, &value)) {
    if (!value->is_string() ||
        !RmadState_Parse(value->GetString(), &current_state_)) {
      // State string in json_store_ is invalid.
      current_state_ = RMAD_STATE_UNKNOWN;
    }
  } else if (RoVerificationKeyPressed()) {
    current_state_ = RMAD_STATE_WELCOME_SCREEN;
    if (!json_store_->SetValue(kRmadCurrentState,
                               base::Value(RmadState_Name(current_state_)))) {
      current_state_ = RMAD_STATE_UNKNOWN;
    }
  } else {
    current_state_ = RMAD_STATE_RMA_NOT_REQUIRED;
  }
}

void RmadInterfaceImpl::GetCurrentState(
    const GetCurrentStateRequest& request,
    const GetCurrentStateCallback& callback) {
  GetCurrentStateReply reply;
  reply.set_state(current_state_);
  callback.Run(reply);
}

void RmadInterfaceImpl::TransitionState(
    const TransitionStateRequest& request,
    const TransitionStateCallback& callback) {
  // TODO(chenghan): Add error replies when failed to get `state_handler`, or
  //                 failed to write `json_store_`.
  auto state_handler = state_handler_manager_->GetStateHandler(current_state_);
  if (state_handler) {
    current_state_ = state_handler->GetNextState();
    json_store_->SetValue(kRmadCurrentState,
                          base::Value(RmadState_Name(current_state_)));
  }

  TransitionStateReply reply;
  reply.set_state(current_state_);
  callback.Run(reply);
}

}  // namespace rmad
