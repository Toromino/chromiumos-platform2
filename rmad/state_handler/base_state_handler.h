// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

class BaseStateHandler : public base::RefCounted<BaseStateHandler> {
 public:
  explicit BaseStateHandler(scoped_refptr<JsonStore> json_store);
  virtual ~BaseStateHandler() = default;

  // Returns the RmadState that the class handles. This can be declared by the
  // macro ASSIGN_STATE(state).
  virtual RmadState GetState() const = 0;

  // Returns whether it's allowed to abort the RMA process from the state. This
  // is not allowed by default, and can be set as allowed by the macro
  // SET_ALLOW_ABORT.
  virtual bool IsAllowAbort() const { return false; }

  // Store the next RmadState in the RMA flow depending on device status and
  // user input (e.g. |json_store_| content) to |next_state|. Return true if a
  // transition is valid, false if the device status is not eligible for a state
  // transition (in this case |next_state| will be the same as GetState()).
  virtual bool GetNextState(RmadState* next_state) const = 0;

 protected:
  scoped_refptr<JsonStore> json_store_;
};

#define ASSIGN_STATE(state) \
  RmadState GetState() const override { return state; }

#define SET_ALLOW_ABORT \
  bool IsAllowAbort() const override { return true; }

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_