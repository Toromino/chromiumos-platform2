// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/gatt.h"

#include <string>
#include <utility>

namespace bluetooth {

Gatt::Gatt(Newblue* newblue, DeviceInterfaceHandler* device_interface_handler)
    : newblue_(newblue),
      device_interface_handler_(device_interface_handler),
      weak_ptr_factory_(this) {
  CHECK(newblue_);
  CHECK(device_interface_handler_);

  device_interface_handler_->AddDeviceObserver(this);
}

Gatt::~Gatt() {
  if (device_interface_handler_)
    device_interface_handler_->RemoveDeviceObserver(this);
}

void Gatt::OnGattConnected(const std::string& device_address,
                           gatt_client_conn_t conn_id) {
  CHECK(!device_address.empty());

  auto services = remote_services_.find(device_address);
  if (services != remote_services_.end()) {
    LOG(WARNING) << "GATT cache for device " << device_address
                 << " was not cleared, clear it";
    remote_services_.erase(services);
  }

  // Start GATT browsing.
  UniqueId transaction_id = GetNextId();
  transactions_.emplace(transaction_id, GattClientOperationType::SERVICES_ENUM);

  GattClientOperationStatus status = newblue_->GattClientEnumServices(
      conn_id, true, transaction_id,
      base::Bind(&Gatt::OnGattClientEnumServices,
                 weak_ptr_factory_.GetWeakPtr()));
  if (status != GattClientOperationStatus::OK) {
    LOG(ERROR) << "Failed to browse GATT for device " << device_address
               << " with conn ID " << conn_id;
    transactions_.erase(transaction_id);
  }

  VLOG(1) << "Start GATT browsing for device " << device_address
          << ", transaction " << transaction_id;
}

void Gatt::OnGattDisconnected(const std::string& device_address,
                              gatt_client_conn_t conn_id) {
  CHECK(!device_address.empty());

  VLOG(1) << "Clear the cached GATT services of device " << device_address;
  remote_services_.erase(device_address);
}

void Gatt::TravPrimaryServices(const std::string& device_address,
                               gatt_client_conn_t conn_id) {
  auto services = remote_services_.find(device_address);

  if (services == remote_services_.end()) {
    LOG(WARNING) << "Failed to find remote services associated with device "
                 << device_address;
    return;
  }

  for (const auto& service_entry : services->second) {
    GattService* service = service_entry.second.get();

    if (!service->primary())
      continue;

    UniqueId transaction_id = GetNextId();
    transactions_.emplace(transaction_id,
                          GattClientOperationType::PRIMARY_SERVICE_TRAV);

    GattClientOperationStatus status = newblue_->GattClientTravPrimaryService(
        conn_id, service->uuid(), transaction_id,
        base::Bind(&Gatt::OnGattClientTravPrimaryService,
                   weak_ptr_factory_.GetWeakPtr()));
    if (status != GattClientOperationStatus::OK) {
      LOG(ERROR) << "Failed to traverse GATT primary service "
                 << service->uuid().canonical_value() << " for device "
                 << device_address << " with conn ID " << conn_id;
      transactions_.erase(transaction_id);
    } else {
      VLOG(1) << "Start traversing GATT primary service "
              << service->uuid().canonical_value() << " for device "
              << device_address << ", transaction " << transaction_id;
    }
  }
}

void Gatt::OnGattClientEnumServices(bool finished,
                                    gatt_client_conn_t conn_id,
                                    UniqueId transaction_id,
                                    Uuid uuid,
                                    bool primary,
                                    uint16_t first_handle,
                                    uint16_t num_handles,
                                    GattClientOperationStatus status) {
  auto transaction = transactions_.find(transaction_id);
  CHECK(transaction != transactions_.end());
  CHECK(transaction->second.type == GattClientOperationType::SERVICES_ENUM);

  if (status != GattClientOperationStatus::OK) {
    LOG(ERROR) << "Error GATT client operation, dropping it";
    return;
  }

  // This may be invoked after device is removed, so we check whether the device
  // is still valid.
  std::string device_address =
      device_interface_handler_->GetAddressByConnectionId(conn_id);
  if (device_address.empty()) {
    LOG(WARNING) << "Unknown GATT connection " << conn_id
                 << " for service enumeration result";
    return;
  }

  // Close the transaction when the service enumeration finished.
  if (finished) {
    VLOG(1) << "GATT browsing finished for device " << device_address
            << ", transaction " << transaction_id;
    transactions_.erase(transaction_id);

    // Start primary services traversal.
    TravPrimaryServices(device_address, conn_id);
    return;
  }

  VLOG(2) << "GATT Browsing continues on device " << device_address
          << ", transaction " << transaction_id << ", found "
          << uuid.canonical_value();

  if (!base::ContainsKey(remote_services_, device_address)) {
    std::map<uint16_t, std::unique_ptr<GattService>> services;
    remote_services_.emplace(device_address, std::move(services));
  }

  auto services = remote_services_.find(device_address);
  services->second.emplace(first_handle,
                           std::make_unique<GattService>(
                               device_address, first_handle,
                               first_handle + num_handles - 1, primary, uuid));
}

void Gatt::OnGattClientTravPrimaryService(
    gatt_client_conn_t conn_id,
    UniqueId transaction_id,
    std::unique_ptr<GattService> service) {
  auto transaction = transactions_.find(transaction_id);
  CHECK(transaction != transactions_.end());
  CHECK(transaction->second.type ==
        GattClientOperationType::PRIMARY_SERVICE_TRAV);

  // This may be invoked after device is removed, so we check whether the device
  // is still valid.
  std::string device_address =
      device_interface_handler_->GetAddressByConnectionId(conn_id);
  if (device_address.empty()) {
    LOG(WARNING) << "Unknown GATT connection " << conn_id
                 << " for primary service traversal result";
    transactions_.erase(transaction_id);
    return;
  }

  if (service == nullptr) {
    LOG(ERROR) << "Primary service traversal failed with device "
               << device_address;
    transactions_.erase(transaction_id);
    return;
  }

  auto services = remote_services_.find(device_address);
  if (services == remote_services_.end()) {
    LOG(WARNING) << "No remote services associated with device "
                 << device_address << ", dropping it";
    transactions_.erase(transaction_id);
    return;
  }

  // If there is service change before the traversal finished where the service
  // is no longer there, we drop the result.
  auto srv = services->second.find(service->first_handle());
  if (srv == services->second.end()) {
    LOG(WARNING) << "Unknown primary service "
                 << service->uuid().canonical_value() << ", dropping it";
    transactions_.erase(transaction_id);
    return;
  }

  VLOG(2) << "Replacing service " << service->uuid().canonical_value()
          << " of device " << device_address
          << " with the traversed one, transaction id " << transaction_id;
  srv->second = std::move(service);
  transactions_.erase(transaction_id);
}

}  // namespace bluetooth
