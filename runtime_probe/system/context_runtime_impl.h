// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_RUNTIME_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_RUNTIME_IMPL_H_

#include "runtime_probe/system/context.h"

namespace runtime_probe {

class ContextRuntimeImpl : public Context {
 public:
  ContextRuntimeImpl() = default;
  ~ContextRuntimeImpl() override = default;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_RUNTIME_IMPL_H_