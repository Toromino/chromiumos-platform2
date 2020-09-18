// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter_impl.h"

#include <utility>

namespace diagnostics {

NetworkDiagnosticsAdapterImpl::NetworkDiagnosticsAdapterImpl() = default;
NetworkDiagnosticsAdapterImpl::~NetworkDiagnosticsAdapterImpl() = default;

void NetworkDiagnosticsAdapterImpl::SetNetworkDiagnosticsRoutines(
    mojo::PendingRemote<
        chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
        network_diagnostics_routines) {
  network_diagnostics_routines_.Bind(std::move(network_diagnostics_routines));
}

void NetworkDiagnosticsAdapterImpl::RunLanConnectivityRoutine(
    MojomLanConnectivityCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(
        chromeos::network_diagnostics::mojom::RoutineVerdict::kNotRun);
    return;
  }
  network_diagnostics_routines_->LanConnectivity(std::move(callback));
}

}  // namespace diagnostics
