// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/device_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/network/fake_shill_client.h"
#include "arc/network/net_util.h"

namespace arc_networkd {

namespace {

class FakeAddressManager : public AddressManager {
 public:
  FakeAddressManager()
      : AddressManager({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
        }) {}

  MacAddress GenerateMacAddress() override {
    static const MacAddress kAddr = {0xf7, 0x69, 0xe5, 0xc4, 0x1f, 0x74};
    return kAddr;
  }
};

class DeviceManagerTest : public testing::Test {
 protected:
  DeviceManagerTest() = default;

  void SetUp() override {}

  std::unique_ptr<DeviceManager> NewManager(bool is_arc_legacy = false) {
    shill_helper_ = std::make_unique<FakeShillClientHelper>();
    auto shill_client = shill_helper_->FakeClient();
    shill_client_ = shill_client.get();
    auto mgr = std::make_unique<DeviceManager>(std::move(shill_client),
                                               &addr_mgr_, is_arc_legacy);
    return mgr;
  }

  FakeShillClient* shill_client_;

 private:
  std::unique_ptr<FakeShillClientHelper> shill_helper_;
  FakeAddressManager addr_mgr_;

  // This is required because of the RT netlink handler.
  base::MessageLoopForIO msg_loop_;
  base::FileDescriptorWatcher watcher_{&msg_loop_};
};

}  // namespace

TEST_F(DeviceManagerTest, MakeEthernetDevices) {
  auto mgr = NewManager();
  auto eth0 = mgr->MakeDevice("eth0");
  const auto& cfg = eth0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_eth0");
  EXPECT_EQ(cfg.guest_ifname(), "eth0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_TRUE(eth0->options().find_ipv6_routes);

  auto usb0 = mgr->MakeDevice("usb0");
  const auto& cfgu = usb0->config();
  EXPECT_EQ(cfgu.host_ifname(), "arc_usb0");
  EXPECT_EQ(cfgu.guest_ifname(), "usb0");
  EXPECT_EQ(cfgu.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 13));
  EXPECT_EQ(cfgu.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 14));
  EXPECT_TRUE(usb0->options().find_ipv6_routes);
}

TEST_F(DeviceManagerTest, MakeWifiDevices) {
  auto mgr = NewManager();
  auto wlan0 = mgr->MakeDevice("wlan0");
  const auto& cfg = wlan0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_wlan0");
  EXPECT_EQ(cfg.guest_ifname(), "wlan0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_TRUE(wlan0->options().find_ipv6_routes);

  auto mlan0 = mgr->MakeDevice("mlan0");
  const auto& cfgm = mlan0->config();
  EXPECT_EQ(cfgm.host_ifname(), "arc_mlan0");
  EXPECT_EQ(cfgm.guest_ifname(), "mlan0");
  EXPECT_EQ(cfgm.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 13));
  EXPECT_EQ(cfgm.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 14));
  EXPECT_TRUE(mlan0->options().find_ipv6_routes);
}

TEST_F(DeviceManagerTest, MakeCellularDevice) {
  auto mgr = NewManager();
  auto wwan0 = mgr->MakeDevice("wwan0");
  const auto& cfg = wwan0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_wwan0");
  EXPECT_EQ(cfg.guest_ifname(), "wwan0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_FALSE(wwan0->options().find_ipv6_routes);
}

TEST_F(DeviceManagerTest, MakeDevice_Android) {
  auto mgr = NewManager();
  auto arc0 = mgr->MakeDevice(kAndroidDevice);
  const auto& cfg = arc0->config();
  EXPECT_EQ(cfg.host_ifname(), "arcbr0");
  EXPECT_EQ(cfg.guest_ifname(), "arc0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 1));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 2));
  EXPECT_FALSE(arc0->options().find_ipv6_routes);
}

TEST_F(DeviceManagerTest, MakeDevice_LegacyAndroid) {
  auto mgr = NewManager(true /* is_arc_legacy */);
  auto arc0 = mgr->MakeDevice(kAndroidLegacyDevice);
  const auto& cfg = arc0->config();
  EXPECT_EQ(cfg.host_ifname(), "arcbr0");
  EXPECT_EQ(cfg.guest_ifname(), "arc0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 1));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 2));
  EXPECT_TRUE(arc0->options().find_ipv6_routes);
}

TEST_F(DeviceManagerTest, MakeVpnTunDevice) {
  auto mgr = NewManager();
  auto tun0 = mgr->MakeDevice("tun0");
  const auto& cfg = tun0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_tun0");
  EXPECT_EQ(cfg.guest_ifname(), "cros_tun0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_FALSE(tun0->options().find_ipv6_routes);
}

TEST_F(DeviceManagerTest, MakeDevice_NoMoreSubnets) {
  auto mgr = NewManager();
  std::vector<std::unique_ptr<Device>> devices;
  for (int i = 0; i < 4; ++i) {
    auto dev = mgr->MakeDevice(base::StringPrintf("%d", i));
    EXPECT_TRUE(dev);
    devices.emplace_back(std::move(dev));
  }
  EXPECT_FALSE(mgr->MakeDevice("x"));
}

TEST_F(DeviceManagerTest, AddNewDevices) {
  auto mgr = NewManager();
  mgr->OnGuestStart(GuestMessage::ARC);
  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                           dbus::ObjectPath("wlan0")};
  auto value = brillo::Any(devices);
  shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_TRUE(mgr->Exists("eth0"));
  EXPECT_TRUE(mgr->Exists("wlan0"));
}

TEST_F(DeviceManagerTest, NoDevicesAddedWhenMultinetDisabled) {
  auto mgr = NewManager(true /* is_arc_legacy */);
  mgr->OnGuestStart(GuestMessage::ARC_LEGACY);
  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                           dbus::ObjectPath("wlan0")};
  auto value = brillo::Any(devices);
  shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_FALSE(mgr->Exists("eth0"));
  EXPECT_FALSE(mgr->Exists("wlan0"));
}

TEST_F(DeviceManagerTest, PreviousDevicesRemoved) {
  auto mgr = NewManager();
  {
    std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                             dbus::ObjectPath("wlan0")};
    auto value = brillo::Any(devices);
    shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
    EXPECT_TRUE(mgr->Exists("eth0"));
    EXPECT_TRUE(mgr->Exists("wlan0"));
  }
  {
    std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                             dbus::ObjectPath("eth1")};
    auto value = brillo::Any(devices);
    shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
    EXPECT_TRUE(mgr->Exists("eth0"));
    EXPECT_TRUE(mgr->Exists("eth1"));
    EXPECT_FALSE(mgr->Exists("wlan0"));
  }
}

}  // namespace arc_networkd