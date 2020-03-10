// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/device.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <map>
#include <utility>

#include <base/bind.h>
#include <base/lazy_instance.h>
#include <base/logging.h>

#include "arc/network/crostini_service.h"
#include "arc/network/net_util.h"

namespace arc_networkd {

Device::Config::Config(const std::string& host_ifname,
                       const std::string& guest_ifname,
                       const MacAddress& guest_mac_addr,
                       std::unique_ptr<Subnet> ipv4_subnet,
                       std::unique_ptr<SubnetAddress> host_ipv4_addr,
                       std::unique_ptr<SubnetAddress> guest_ipv4_addr,
                       std::unique_ptr<Subnet> lxd_ipv4_subnet)
    : host_ifname_(host_ifname),
      guest_ifname_(guest_ifname),
      guest_mac_addr_(guest_mac_addr),
      ipv4_subnet_(std::move(ipv4_subnet)),
      host_ipv4_addr_(std::move(host_ipv4_addr)),
      guest_ipv4_addr_(std::move(guest_ipv4_addr)),
      lxd_ipv4_subnet_(std::move(lxd_ipv4_subnet)) {}

Device::Device(const std::string& ifname,
               std::unique_ptr<Device::Config> config,
               const Device::Options& options)
    : ifname_(ifname), config_(std::move(config)), options_(options) {
  DCHECK(config_);
}

const std::string& Device::ifname() const {
  return ifname_;
}

Device::Config& Device::config() const {
  CHECK(config_);
  return *config_.get();
}

const Device::Options& Device::options() const {
  return options_;
}

void Device::set_tap_ifname(const std::string& tap_ifname) {
  tap_ = tap_ifname;
}

const std::string& Device::tap_ifname() const {
  return tap_;
}

bool Device::UsesDefaultInterface() const {
  return options_.use_default_interface;
}

std::ostream& operator<<(std::ostream& stream, const Device& device) {
  stream << "{ ifname: " << device.ifname_
         << ", bridge_ifname: " << device.config_->host_ifname()
         << ", bridge_ipv4_addr: "
         << device.config_->host_ipv4_addr_->ToCidrString()
         << ", guest_ifname: " << device.config_->guest_ifname()
         << ", guest_ipv4_addr: "
         << device.config_->guest_ipv4_addr_->ToCidrString()
         << ", guest_mac_addr: "
         << MacAddressToString(device.config_->guest_mac_addr())
         << ", fwd_multicast: " << device.options_.fwd_multicast
         << ", ipv6_enabled: " << device.options_.ipv6_enabled << '}';
  return stream;
}

}  // namespace arc_networkd
