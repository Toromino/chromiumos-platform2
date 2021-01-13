// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/scan_manager.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "bluetooth/newblued/util.h"
#include <chromeos/dbus/service_constants.h>

namespace bluetooth {

ScanManager::ScanManager(Newblue* newblue,
                         DeviceInterfaceHandler* device_interface_handler,
                         ExportedInterface* adapter_interface)
    : newblue_(newblue),
      device_interface_handler_(device_interface_handler),
      adapter_interface_(adapter_interface),
      weak_ptr_factory_(this) {
  CHECK(newblue_);
  CHECK(device_interface_handler_);

  device_interface_handler_->AddDeviceObserver(this);
}

ScanManager::~ScanManager() {
  if (device_interface_handler_)
    device_interface_handler_->RemoveDeviceObserver(this);
}

void ScanManager::OnGattConnected(const std::string& device_address,
                                  gatt_client_conn_t conn_id) {
  VLOG(2) << __func__;
  paired_devices_[device_address].is_connected = true;
  UpdateBackgroundScan();
}

void ScanManager::OnGattDisconnected(const std::string& device_address,
                                     gatt_client_conn_t conn_id,
                                     bool is_disconnected_by_newblue) {
  VLOG(2) << __func__;
  paired_devices_[device_address].is_connected = false;
  paired_devices_[device_address].is_disconnected_by_newblue =
      is_disconnected_by_newblue;
  UpdateBackgroundScan();
}

void ScanManager::OnDevicePaired(const std::string& device_address) {
  VLOG(2) << __func__;
  PairedDevice paired_device;
  paired_devices_.emplace(device_address, paired_device);
  UpdateBackgroundScan();
}

void ScanManager::OnDeviceUnpaired(const std::string& device_address) {
  VLOG(2) << __func__;
  paired_devices_.erase(device_address);
  UpdateBackgroundScan();
}

bool ScanManager::SetFilter(const std::string client_id,
                            const brillo::VariantDictionary& filter) {
  VLOG(2) << __func__;

  // If failed to parse the filter parameters return with false.
  if (!ParseAndSaveFilter(client_id, filter))
    return false;

  // If the there is no scanning activity or the client has not requested a
  // scan. Postpone the filter merging.
  if (scan_state_ == ScanState::IDLE ||
      std::find(clients_.begin(), clients_.end(), client_id) ==
          clients_.end()) {
    return true;
  }

  MergeFilters();
  return true;
}

bool ScanManager::ParseAndSaveFilter(const std::string client_id,
                                     const brillo::VariantDictionary& filter) {
  // Initialize the filter struct with minimum RSSI requirement, largest
  // pathloss tolerance, and none UUIDs filters.
  Filter parsing_filter{SHRT_MIN, USHRT_MAX, std::set<Uuid>()};

  // When this method is called with no filter parameter, filter is removed.
  if (filter.empty()) {
    filters_[client_id] = Filter();
    VLOG(2) << "Filter removed for client: " << client_id;
    return true;
  }

  // Parse and save the filter parameters.
  GetVariantValue<int16_t>(filter, "RSSI", &parsing_filter.rssi);
  GetVariantValue<uint16_t>(filter, "Pathloss", &parsing_filter.pathloss);
  GetVariantValue<std::set<Uuid>>(filter, "UUIDs", &parsing_filter.uuids);
  filters_[client_id] = parsing_filter;

  VLOG(2) << "Scan Filter Parameters: |RSSI = " << parsing_filter.rssi
          << "|Pathloss = " << parsing_filter.pathloss
          << "|# of UUIDs = " << parsing_filter.uuids.size() << "|";

  return true;
}

void ScanManager::MergeFilters(void) {
  VLOG(2) << __func__;

  // Check if there is any active clients. Set the master flag accordingly.
  if (clients_.empty()) {
    is_filtered_scan_ = false;
    VLOG(2) << "Filter Scan: is_filtered_scan_ = " << is_filtered_scan_;
    return;
  }
  // Initialize the filter struct with maximum RSSI requirement, smallest
  // pathloss tolerance, and none UUIDs filters.
  Filter merged_filter{SHRT_MAX, 0, std::set<Uuid>()};
  bool is_filter_by_uuid = true;

  for (auto item : filters_) {
    // If the client, who own this filter has not requested a scan. Do not
    // merge this filter.
    if (std::find(clients_.begin(), clients_.end(), item.first) ==
        clients_.end()) {
      continue;
    }

    // Choose the lower rssi and higher pathloss value.
    if (item.second.rssi < merged_filter.rssi)
      merged_filter.rssi = item.second.rssi;
    if (item.second.pathloss > merged_filter.pathloss)
      merged_filter.pathloss = item.second.pathloss;

    // If client passed uuid parameter with no list entry, disable uuid
    // filtering, allow all to pass.
    if (is_filter_by_uuid) {
      if (item.second.uuids.size() == 0) {
        is_filter_by_uuid = false;
        merged_filter.uuids.clear();
        continue;
      }
      // Insert the UUID into merged filter UUID list and check for duplicates.
      for (auto uuid : item.second.uuids) {
        merged_filter.uuids.insert(uuid);
      }
    }
  }

  merged_filter_ = merged_filter;
  is_filtered_scan_ = merged_filter_.rssi != SHRT_MIN ||
                      merged_filter_.pathloss != USHRT_MAX || is_filter_by_uuid;

  VLOG(2) << "Merged Filter Parameters: |is_filtered_scan = "
          << is_filtered_scan_ << "|RSSI = " << merged_filter_.rssi
          << "|Pathloss = " << merged_filter_.pathloss
          << "|# of UUIDs = " << merged_filter_.uuids.size() << "|";
}

bool ScanManager::IsFilterMatch(const DeviceInfo& device_info) const {
  VLOG(2) << __func__;

  if (!is_filtered_scan_)
    return true;
  if (device_info.rssi < merged_filter_.rssi &&
      device_info.tx_power - device_info.rssi > merged_filter_.pathloss)
    return false;
  if (merged_filter_.uuids.empty())
    return true;
  for (auto uuid : merged_filter_.uuids) {
    if (device_info.service_uuids.find(uuid) != device_info.service_uuids.end())
      return true;
  }
  return false;
}

bool ScanManager::StartScan(std::string client_id) {
  clients_.push_back(client_id);
  // Create and initialize a new filter for the client if not exist yet.
  if (!base::ContainsKey(filters_, client_id))
    filters_[client_id] = Filter();
  MergeFilters();
  if (!UpdateScan()) {
    clients_.pop_back();
    filters_.erase(client_id);
    return false;
  }
  return true;
}

bool ScanManager::StopScan(std::string client_id) {
  clients_.erase(std::remove(clients_.begin(), clients_.end(), client_id),
                 clients_.end());
  MergeFilters();
  if (!UpdateScan()) {
    clients_.push_back(client_id);
    return false;
  }
  filters_.erase(client_id);
  return true;
}

bool ScanManager::UpdateScanSuspensionState(bool is_in_suspension) {
  is_in_suspension_ = is_in_suspension;
  if (!UpdateScan())
    return false;

  return true;
}

bool ScanManager::UpdateScan(void) {
  ScanState scan_state_new;
  number_of_clients_ = clients_.size();

  if (is_in_suspension_) {
    // Stop scan if suspend is in progress.
    scan_state_new = ScanState::IDLE;
  } else if (number_of_clients_ > 0) {
    // Start active scan if a client is requesting and system not in suspension.
    scan_state_new = ScanState::ACTIVE_SCAN;
  } else if (needs_background_scan_) {
    // Check if background scan is needed.
    scan_state_new = ScanState::PASSIVE_SCAN;
  } else {
    scan_state_new = ScanState::IDLE;
  }

  if (scan_state_ == scan_state_new) {
    VLOG(2) << "No need to change scan state";
    return true;
  }

  VLOG(2) << "Scan Manager scan state change from: " << (uint8_t)scan_state_
          << " to: " << (uint8_t)scan_state_new;

  switch (scan_state_new) {
    case ScanState::IDLE:
      if (!newblue_->StopDiscovery())
        return false;
      VLOG(1) << "Scan Manager: Stop scan.";
      break;
    case ScanState::ACTIVE_SCAN:
      // If from passive scanning, stop it first then restart with active.
      // settings.
      if (scan_state_ == ScanState::PASSIVE_SCAN) {
        if (!newblue_->StopDiscovery()) {
          LOG(ERROR) << "Scan Manger received failed to stop discovery from "
                        "libnewblue.";
          return false;
        }
        // Update state to IDLE in case start scan failed later.
        scan_state_ = ScanState::IDLE;
      }

      if (!newblue_->StartDiscovery(
              profiles_["active-scan"].active,
              profiles_["active-scan"].scan_interval,
              profiles_["active-scan"].scan_window,
              profiles_["active-scan"].use_randomAddr,
              profiles_["active-scan"].only_whitelist,
              profiles_["active-scan"].filter_duplicates,
              base::Bind(&ScanManager::DeviceDiscoveryCallback,
                         weak_ptr_factory_.GetWeakPtr()))) {
        LOG(ERROR) << "Scan Manger received failed to start discovery from "
                      "libnewblue.";
        return false;
      }
      VLOG(1) << "Scan Manager: Start active scan.";
      break;
    case ScanState::PASSIVE_SCAN:
      // If from active scanning, stop it first then restart with passive
      // settings.
      if (scan_state_ == ScanState::ACTIVE_SCAN) {
        if (!newblue_->StopDiscovery()) {
          LOG(ERROR) << "Scan Manger received failed to stop discovery from "
                        "libnewblue.";
          return false;
        }
        // Update state to IDLE in case start scan failed later.
        scan_state_ = ScanState::IDLE;
      }
      if (!newblue_->StartDiscovery(
              profiles_["passive-scan"].active,
              profiles_["passive-scan"].scan_interval,
              profiles_["passive-scan"].scan_window,
              profiles_["passive-scan"].use_randomAddr,
              profiles_["passive-scan"].only_whitelist,
              profiles_["passive-scan"].filter_duplicates,
              base::Bind(&ScanManager::DeviceDiscoveryCallback,
                         weak_ptr_factory_.GetWeakPtr()))) {
        LOG(ERROR) << "Scan Manger received failed to start discovery from "
                      "libnewblue.";
        return false;
      }
      VLOG(1) << "Scan Manager: Start passive scan.";
      break;
  }

  scan_state_ = scan_state_new;
  adapter_interface_
      ->EnsureExportedPropertyRegistered<bool>(
          bluetooth_adapter::kDiscoveringProperty)
      ->SetValue(scan_state_ == ScanState::ACTIVE_SCAN ||
                 scan_state_ == ScanState::PASSIVE_SCAN);

  return true;
}

void ScanManager::UpdateBackgroundScan() {
  needs_background_scan_ = false;
  // If a device is paired but not connected and is not disconnected
  // intentionally by newblue, background scannning is needed.
  for (const auto& kv : paired_devices_) {
    if (!(kv.second.is_connected || kv.second.is_disconnected_by_newblue))
      needs_background_scan_ = true;
  }
  VLOG(2) << "Background scan needed: "
          << (needs_background_scan_ ? "Yes" : "No");
  UpdateScan();
}

void ScanManager::DeviceDiscoveryCallback(const std::string& adv_address,
                                          uint8_t address_type,
                                          const std::string& resolved_address,
                                          int8_t rssi,
                                          uint8_t reply_type,
                                          const std::vector<uint8_t>& eir) {
  bool has_active_discovery_client = number_of_clients_ > 0;
  DeviceInfo device_info(has_active_discovery_client, adv_address, address_type,
                         resolved_address, rssi, reply_type);
  ParseEir(&device_info, eir);
  if (IsFilterMatch(device_info))
    device_interface_handler_->OnDeviceDiscovered(device_info);
}

void ScanManager::ParseEir(DeviceInfo* device_info,
                           const std::vector<uint8_t>& eir) {
  CHECK(device_info);

  uint8_t pos = 0;
  std::set<Uuid> service_uuids;
  std::map<Uuid, std::vector<uint8_t>> service_data;

  while (pos + 1 < eir.size()) {
    uint8_t field_len = eir[pos];

    // End of EIR
    if (field_len == 0)
      break;

    // Corrupt EIR data
    if (pos + field_len >= eir.size())
      break;

    EirType eir_type = static_cast<EirType>(eir[pos + 1]);
    const uint8_t* data = &eir[pos + 2];

    // A field consists of 1 byte field type + data:
    // | 1-byte field_len | 1-byte type | (field length - 1) bytes data ... |
    uint8_t data_len = field_len - 1;

    switch (eir_type) {
      case EirType::FLAGS:
        // No default value should be set for flags according to Supplement to
        // the Bluetooth Core Specification. Flags field can be 0 or more octets
        // long. If the length is 1, then flags[0] is octet[0]. Store only
        // octet[0] for now due to lack of definition of the following octets
        // in Supplement to the Bluetooth Core Specification.
        if (data_len > 0)
          device_info->flags = std::vector<uint8_t>(data, data + 1);
        // If |data_len| is 0, we avoid setting zero-length advertising flags as
        // this currently causes Chrome to crash.
        // TODO(crbug.com/876908): Fix Chrome to not crash with zero-length
        // advertising flags.
        break;

      // If there are more than one instance of either COMPLETE or INCOMPLETE
      // type of a UUID size, the later one(s) will be cached as well.
      case EirType::UUID16_INCOMPLETE:
      case EirType::UUID16_COMPLETE:
        ParseDataIntoUuids(&service_uuids, kUuid16Size, data, data_len);
        break;
      case EirType::UUID32_INCOMPLETE:
      case EirType::UUID32_COMPLETE:
        ParseDataIntoUuids(&service_uuids, kUuid32Size, data, data_len);
        break;
      case EirType::UUID128_INCOMPLETE:
      case EirType::UUID128_COMPLETE:
        ParseDataIntoUuids(&service_uuids, kUuid128Size, data, data_len);
        break;

      // Name
      case EirType::NAME_SHORT:
      case EirType::NAME_COMPLETE: {
        char c_name[HCI_DEV_NAME_LEN + 1];

        // Some device has trailing '\0' at the end of the name data.
        // So make sure we only take the characters before '\0' and limited
        // to the max length allowed by Bluetooth spec (HCI_DEV_NAME_LEN).
        uint8_t name_len =
            std::min(data_len, static_cast<uint8_t>(HCI_DEV_NAME_LEN));
        strncpy(c_name, reinterpret_cast<const char*>(data), name_len);
        c_name[name_len] = '\0';
        device_info->name = ConvertToAsciiString(c_name) + kNewblueNameSuffix;
      } break;

      case EirType::TX_POWER:
        if (data_len == 1)
          device_info->tx_power = (static_cast<int8_t>(*data));
        break;
      case EirType::CLASS_OF_DEV:
        // 24-bit little endian data
        if (data_len == 3)
          device_info->eir_class = (GetNumFromLE24(data));
        break;

      // If the UUID already exists, the service data will be updated.
      case EirType::SVC_DATA16:
        ParseDataIntoServiceData(&service_data, kUuid16Size, data, data_len);
        break;
      case EirType::SVC_DATA32:
        ParseDataIntoServiceData(&service_data, kUuid32Size, data, data_len);
        break;
      case EirType::SVC_DATA128:
        ParseDataIntoServiceData(&service_data, kUuid128Size, data, data_len);
        break;

      case EirType::GAP_APPEARANCE:
        // 16-bit little endian data
        if (data_len == 2) {
          uint16_t appearance = GetNumFromLE16(data);
          device_info->appearance = (appearance);
          device_info->icon = (ConvertAppearanceToIcon(appearance));
        }
        break;
      case EirType::MANUFACTURER_DATA:
        if (data_len >= 2) {
          // The order of manufacturer data is not specified explicitly in
          // Supplement to the Bluetooth Core Specification, so the original
          // order used in BlueZ is adopted.
          device_info->manufacturer = (ParseDataIntoManufacturer(
              GetNumFromLE16(data),
              std::vector<uint8_t>(data + 2,
                                   data + std::max<uint8_t>(data_len, 2))));
        }
        break;
      default:
        // Do nothing for unhandled EIR type.
        break;
    }

    pos += field_len + 1;
  }

  // This is different from BlueZ where it memorizes all service UUIDs and
  // service data ever received for the same device. If there is no service
  // UUIDs/service data being presented, service UUIDs/servicedata will not
  // be updated.
  if (!service_uuids.empty())
    device_info->service_uuids = (std::move(service_uuids));
  if (!service_data.empty())
    device_info->service_data = (std::move(service_data));
}

}  // namespace bluetooth