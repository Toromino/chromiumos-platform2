// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/mock_vpn_driver.h"

namespace shill {

MockVPNDriver::MockVPNDriver() : VPNDriver(nullptr, nullptr, nullptr, 0) {}

MockVPNDriver::~MockVPNDriver() = default;

MockVPNDriverEventHandler::MockVPNDriverEventHandler() = default;

MockVPNDriverEventHandler::~MockVPNDriverEventHandler() = default;

}  // namespace shill
