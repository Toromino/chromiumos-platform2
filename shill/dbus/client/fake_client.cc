// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/client/fake_client.h"

namespace shill {

FakeClient::FakeClient(scoped_refptr<dbus::Bus> bus) : Client(bus) {}

void FakeClient::Init() {
  init_ = true;
}

void FakeClient::RegisterProcessChangedHandler(
    const base::RepeatingCallback<void(bool)>& handler) {
  process_handler_ = handler;
}

void FakeClient::RegisterDefaultServiceChangedHandler(
    const FakeClient::DefaultServiceChangedHandler& handler) {
  default_service_handlers_.push_back(handler);
}

void FakeClient::RegisterDefaultDeviceChangedHandler(
    const FakeClient::DeviceChangedHandler& handler) {
  default_device_handlers_.push_back(handler);
}

void FakeClient::RegisterDeviceChangedHandler(
    const FakeClient::DeviceChangedHandler& handler) {
  device_handlers_.push_back(handler);
}

void FakeClient::RegisterDeviceAddedHandler(
    const FakeClient::DeviceChangedHandler& handler) {
  device_added_handlers_.push_back(handler);
}

void FakeClient::RegisterDeviceRemovedHandler(
    const FakeClient::DeviceChangedHandler& handler) {
  device_removed_handlers_.push_back(handler);
}

std::unique_ptr<Client::ManagerPropertyAccessor> FakeClient::ManagerProperties(
    const base::TimeDelta& timeout) const {
  return nullptr;
}

}  // namespace shill