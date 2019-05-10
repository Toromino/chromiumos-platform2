// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_THIRD_PARTY_VPN_DRIVER_H_
#define SHILL_VPN_THIRD_PARTY_VPN_DRIVER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <gtest/gtest_prod.h>

#include "shill/ipconfig.h"
#include "shill/net/io_handler.h"
#include "shill/refptr_types.h"
#include "shill/service.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class ControlInterface;
class DeviceInfo;
class Error;
class FileIO;
class IOHandlerFactory;
class Metrics;
class ThirdPartyVpnAdaptorInterface;

class ThirdPartyVpnDriver : public VPNDriver {
 public:
  enum PlatformMessage {
    kConnected = 1,
    kDisconnected,
    kError,
    kLinkDown,
    kLinkUp,
    kLinkChanged,
    kSuspend,
    kResume
  };

  ThirdPartyVpnDriver(ControlInterface* control, EventDispatcher* dispatcher,
                      Metrics* metrics, Manager* manager,
                      DeviceInfo* device_info);
  ~ThirdPartyVpnDriver() override;

  // UpdateConnectionState is called by DBus adaptor when
  // "UpdateConnectionState" method is called on the DBus interface.
  void UpdateConnectionState(Service::ConnectState connection_state,
                             std::string* error_message);

  // SendPacket is called by the DBus adaptor when "SendPacket" method is called
  // on the DBus interface.
  void SendPacket(const std::vector<uint8_t>& data, std::string* error_message);

  // SetParameters is called by the DBus adaptor when "SetParameter" method is
  // called on the DBus interface.
  void SetParameters(const std::map<std::string, std::string>& parameters,
                     std::string* error_message, std::string* warning_message);

  void ClearExtensionId(Error* error);
  bool SetExtensionId(const std::string& value, Error* error);

  // Implementation of VPNDriver
  void InitPropertyStore(PropertyStore* store) override;
  bool ClaimInterface(const std::string& link_name,
                      int interface_index) override;
  void Connect(const VPNServiceRefPtr& service, Error* error) override;
  std::string GetProviderType() const override;
  void Disconnect() override;
  void OnConnectionDisconnected() override;
  void OnDefaultServiceChanged(const ServiceRefPtr& service);
  void OnDefaultServiceStateChanged(const ServiceRefPtr& service) override;
  bool Load(StoreInterface* storage, const std::string& storage_id) override;
  bool Save(StoreInterface* storage, const std::string& storage_id,
            bool save_credentials) override;

  void OnBeforeSuspend(const ResultCallback& callback) override;
  void OnAfterResume() override;

  const std::string& object_path_suffix() const { return object_path_suffix_; }

 protected:
  void OnConnectTimeout() override;

 private:
  friend class ThirdPartyVpnDriverTest;
  FRIEND_TEST(ThirdPartyVpnDriverTest, ConnectAndDisconnect);
  FRIEND_TEST(ThirdPartyVpnDriverTest, ReconnectionEvents);
  FRIEND_TEST(ThirdPartyVpnDriverTest, PowerEvents);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SetParameters);
  FRIEND_TEST(ThirdPartyVpnDriverTest, UpdateConnectionState);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SendPacket);

  // Implements the public IdleService and FailService methods. Resets the VPN
  // state and deallocates all resources. If there's a service associated
  // through Connect, sets its state |state|; if |state| is
  // Service::kStateFailure, sets the failure reason to |failure| and its
  // ErrorDetails property to |error_details|; disassociates from the service.
  // Closes the handle to tun device, IO handler if open and deactivates itself
  // with the |thirdpartyvpn_adaptor_| if active.
  void Cleanup(Service::ConnectState state, Service::ConnectFailure failure,
               const std::string& error_details);

  // This function first checks if a value is present for a particular |key| in
  // the dictionary |parameters|.
  // If present it ensures the value is a valid IP address and then sets it to
  // the |target|.
  // The flag |mandatory| when set to true, makes the function treat a missing
  // key as an error. The function adds to |error_messages|, when there is a
  // failure.
  // This function supports only IPV4 addresses now.
  void ProcessIp(const std::map<std::string, std::string>& parameters,
                 const char* key, std::string* target, bool mandatory,
                 std::string* error_message);

  // This function first checks if a value is present for a particular |key| in
  // the dictionary |parameters|.
  // If present it treats the value as a list of string separated by
  // |delimiter|. Each string value is verified to be a valid IP address,
  // deleting ones that are not. The list of string is set to |target|.
  // The flag |mandatory| when set to true, makes the function treat a missing
  // key as an error. The function adds to |error_message|, when there is a
  // failure and |warn_message| when there is a warning.
  void ProcessIPArray(
      const std::map<std::string, std::string>& parameters, const char* key,
      char delimiter, std::vector<std::string>* target, bool mandatory,
      std::string* error_message, std::string* warn_message);

  // This function first checks if a value is present for a particular |key| in
  // the dictionary |parameters|.
  // If present it treats the value as a list of string separated by
  // |delimiter|. Each string value is verified to be a valid IP address in
  // CIDR format, deleting ones that are not. The list of string is set to
  // |target|. The flag |mandatory| when set to true, makes the function treat a
  // missing key as an error. The function adds to |error_message|, when there
  // is a failure and |warn_message| when there is a warning.
  void ProcessIPArrayCIDR(
      const std::map<std::string, std::string>& parameters, const char* key,
      char delimiter, std::vector<std::string>* target, bool mandatory,
      std::string* error_message, std::string* warn_message);

  // This function first checks if a value is present for a particular |key| in
  // the dictionary |parameters|.
  // If present it treats the value as a list of string separated by
  // |delimiter|. The list of string is set to |target|.
  // The flag |mandatory| when set to true, makes the function treat a missing
  // key as an error. The function adds to |error_messages|, when there is a
  // failure.
  void ProcessSearchDomainArray(
      const std::map<std::string, std::string>& parameters, const char* key,
      char delimiter, std::vector<std::string>* target, bool mandatory,
      std::string* error_message);

  // This function first checks if a value is present for a particular |key| in
  // the dictionary |parameters|.
  // If present it treats the value as an integer and verifies if the value lies
  // between |min_value| and |max_value|. It then updates |target| with the
  // integer value if it is in range.
  // The flag |mandatory| when set to true, makes the function treat a missing
  // key as an error. The function adds to |error_messages|, when there is a
  // failure.
  void ProcessInt32(const std::map<std::string, std::string>& parameters,
                    const char* key, int32_t* target, int32_t min_value,
                    int32_t max_value, bool mandatory,
                    std::string* error_message);

  // This function first checks if a value is present for a particular |key| in
  // the dictionary |parameters|.
  // If present it treats the value as a boolean. It then updates |target|
  // with the boolean value if it is valid;
  // The flag |mandatory| when set to true, makes the function treat a missing
  // key as an error. The function adds to |error_messages|, when there is a
  // failure.
  void ProcessBoolean(const std::map<std::string, std::string>& parameters,
                      const char* key, bool* target, bool mandatory,
                      std::string* error_message);

  // These functions are called whe there is input and error in the tun
  // interface.
  void OnInput(InputData* data);
  void OnInputError(const std::string& error);

  // This function is called when a new default service first comes online,
  // so the app knows it needs to reconnect to the VPN gateway.
  void TriggerReconnect(const ServiceRefPtr& service);

  static const Property kProperties[];

  // This variable keeps track of the active instance. There can be multiple
  // instance of this class at a time but only one would be active that can
  // communicate with the VPN client over DBUS.
  static ThirdPartyVpnDriver* active_client_;

  ControlInterface* control_;
  Metrics* metrics_;
  DeviceInfo* device_info_;

  // ThirdPartyVpnAdaptorInterface manages the DBus communication and provides
  // an unique identifier for the ThirdPartyVpnDriver.
  std::unique_ptr<ThirdPartyVpnAdaptorInterface> adaptor_interface_;

  // Object path suffix is made of Extension ID and name that collectively
  // identifies the configuration of the third party VPN client.
  std::string object_path_suffix_;

  // File descriptor for the tun device.
  int tun_fd_;

  // A pointer to the VPN service.
  VPNServiceRefPtr service_;

  // Name of the tunnel interface clone.
  std::string tunnel_interface_;

  // A pointer to the virtual VPN device created on connect.
  VirtualDeviceRefPtr device_;

  // Configuration properties of the virtual VPN device set by the VPN client.
  IPConfig::Properties ip_properties_;
  bool ip_properties_set_;

  IOHandlerFactory* io_handler_factory_;

  // IO handler triggered when there is an error or data ready for read in the
  // tun device.
  std::unique_ptr<IOHandler> io_handler_;

  // The object is used to write to tun device.
  FileIO* file_io_;

  // Set used to identify duplicate entries in inclusion and exclusion list.
  std::set<std::string> known_cidrs_;

  // The boolean indicates if parameters are expected from the VPN client.
  bool parameters_expected_;

  // Default service watch callback tag.
  int default_service_callback_tag_;

  // Flag indicating whether the extension supports reconnections - a feature
  // that wasn't in the original API.  If not, we won't send link_* or
  // suspend/resume signals.
  bool reconnect_supported_;

  // Helps distinguish between a network->network transition (where the
  // client simply reconnects), and a network->link_down->network transition
  // (where the client should disconnect, wait for link up, then reconnect).
  bool link_down_;

  DISALLOW_COPY_AND_ASSIGN(ThirdPartyVpnDriver);
};

}  // namespace shill

#endif  // SHILL_VPN_THIRD_PARTY_VPN_DRIVER_H_
