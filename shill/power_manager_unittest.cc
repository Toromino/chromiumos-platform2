// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/power_manager.h"

#include <string>

#include <base/bind.h>
#include <base/memory/scoped_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_event_dispatcher.h"
#include "shill/mock_power_manager_proxy.h"
#include "shill/power_manager_proxy_interface.h"
#include "shill/proxy_factory.h"

using base::Bind;
using base::Unretained;
using std::map;
using std::string;
using testing::_;
using testing::Return;
using testing::Test;

namespace shill {

namespace {

class FakeProxyFactory : public ProxyFactory {
 public:
  FakeProxyFactory()
      : delegate_(NULL),
        proxy_(new MockPowerManagerProxy) {}

  virtual PowerManagerProxyInterface *CreatePowerManagerProxy(
      PowerManagerProxyDelegate *delegate) {
    delegate_ = delegate;
    return proxy_;
  }
  PowerManagerProxyDelegate *delegate() const { return delegate_; }
  MockPowerManagerProxy *proxy() const { return proxy_; }

 private:
  PowerManagerProxyDelegate *delegate_;
  MockPowerManagerProxy *const proxy_;
};

}  // namespace

class PowerManagerTest : public Test {
 public:
  static const char kKey1[];
  static const char kKey2[];
  static const int kSuspendId1 = 123;
  static const int kSuspendId2 = 456;

  PowerManagerTest()
      : power_manager_(&dispatcher_, &factory_),
        delegate_(factory_.delegate()) {
    state_change_callback1_ =
        Bind(&PowerManagerTest::StateChangeAction1, Unretained(this));
    state_change_callback2_ =
        Bind(&PowerManagerTest::StateChangeAction2, Unretained(this));
    suspend_delay_callback1_ =
        Bind(&PowerManagerTest::SuspendDelayAction1, Unretained(this));
    suspend_delay_callback2_ =
        Bind(&PowerManagerTest::SuspendDelayAction2, Unretained(this));
  }

  MOCK_METHOD1(StateChangeAction1, void(PowerManager::SuspendState));
  MOCK_METHOD1(StateChangeAction2, void(PowerManager::SuspendState));
  MOCK_METHOD1(SuspendDelayAction1, void(int));
  MOCK_METHOD1(SuspendDelayAction2, void(int));

 protected:
  void OnSuspendImminent(int suspend_id) {
    EXPECT_CALL(dispatcher_,
                PostDelayedTask(_, PowerManager::kSuspendTimeoutMilliseconds));
    factory_.delegate()->OnSuspendImminent(suspend_id);
    EXPECT_EQ(PowerManagerProxyDelegate::kSuspending,
              power_manager_.power_state());
  }

  void OnSuspendTimeout() {
    power_manager_.OnSuspendTimeout();
  }

  MockEventDispatcher dispatcher_;
  FakeProxyFactory factory_;
  PowerManager power_manager_;
  PowerManagerProxyDelegate *const delegate_;
  PowerManager::PowerStateCallback state_change_callback1_;
  PowerManager::PowerStateCallback state_change_callback2_;
  PowerManager::SuspendDelayCallback suspend_delay_callback1_;
  PowerManager::SuspendDelayCallback suspend_delay_callback2_;
};

const char PowerManagerTest::kKey1[] = "Zaphod";
const char PowerManagerTest::kKey2[] = "Beeblebrox";

TEST_F(PowerManagerTest, OnPowerStateChanged) {
  EXPECT_EQ(PowerManagerProxyDelegate::kUnknown, power_manager_.power_state());
  power_manager_.OnPowerStateChanged(PowerManagerProxyDelegate::kOn);
  EXPECT_EQ(PowerManagerProxyDelegate::kOn, power_manager_.power_state());
}

TEST_F(PowerManagerTest, AddStateChangeCallback) {
  EXPECT_CALL(*this, StateChangeAction1(PowerManagerProxyDelegate::kOn));
  power_manager_.AddStateChangeCallback(kKey1, state_change_callback1_);
  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kOn);
  power_manager_.RemoveStateChangeCallback(kKey1);
}

TEST_F(PowerManagerTest, AddSuspendDelayCallback) {
  EXPECT_CALL(*this, SuspendDelayAction1(kSuspendId1));
  power_manager_.AddSuspendDelayCallback(kKey1, suspend_delay_callback1_);
  EXPECT_EQ(PowerManagerProxyDelegate::kUnknown, power_manager_.power_state());
  OnSuspendImminent(kSuspendId1);
  power_manager_.RemoveSuspendDelayCallback(kKey1);
}

TEST_F(PowerManagerTest, AddMultipleStateChangeRunMultiple) {
  EXPECT_CALL(*this, StateChangeAction1(PowerManagerProxyDelegate::kOn));
  EXPECT_CALL(*this, StateChangeAction1(PowerManagerProxyDelegate::kMem));
  power_manager_.AddStateChangeCallback(kKey1, state_change_callback1_);

  EXPECT_CALL(*this, StateChangeAction2(PowerManagerProxyDelegate::kOn));
  EXPECT_CALL(*this, StateChangeAction2(PowerManagerProxyDelegate::kMem));
  power_manager_.AddStateChangeCallback(kKey2, state_change_callback2_);

  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kOn);
  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kMem);
}

TEST_F(PowerManagerTest, AddMultipleSuspendDelayRunMultiple) {
  EXPECT_CALL(*this, SuspendDelayAction1(kSuspendId1));
  EXPECT_CALL(*this, SuspendDelayAction1(kSuspendId2));
  power_manager_.AddSuspendDelayCallback(kKey1, suspend_delay_callback1_);

  EXPECT_CALL(*this, SuspendDelayAction2(kSuspendId1));
  EXPECT_CALL(*this, SuspendDelayAction2(kSuspendId2));
  power_manager_.AddSuspendDelayCallback(kKey2, suspend_delay_callback2_);

  OnSuspendImminent(kSuspendId1);
  OnSuspendImminent(kSuspendId2);
}

TEST_F(PowerManagerTest, RemoveStateChangeCallback) {
  EXPECT_CALL(*this, StateChangeAction1(PowerManagerProxyDelegate::kOn));
  EXPECT_CALL(*this, StateChangeAction1(PowerManagerProxyDelegate::kMem));
  power_manager_.AddStateChangeCallback(kKey1, state_change_callback1_);

  EXPECT_CALL(*this, StateChangeAction2(PowerManagerProxyDelegate::kOn));
  EXPECT_CALL(*this, StateChangeAction2(PowerManagerProxyDelegate::kMem))
      .Times(0);
  power_manager_.AddStateChangeCallback(kKey2, state_change_callback2_);

  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kOn);

  power_manager_.RemoveStateChangeCallback(kKey2);
  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kMem);

  power_manager_.RemoveStateChangeCallback(kKey1);
  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kOn);
}

TEST_F(PowerManagerTest, RemoveSuspendDelayCallback) {
  EXPECT_CALL(*this, SuspendDelayAction1(kSuspendId1));
  EXPECT_CALL(*this, SuspendDelayAction1(kSuspendId2));
  power_manager_.AddSuspendDelayCallback(kKey1, suspend_delay_callback1_);

  EXPECT_CALL(*this, SuspendDelayAction2(kSuspendId1));
  EXPECT_CALL(*this, SuspendDelayAction2(kSuspendId2)).Times(0);
  power_manager_.AddSuspendDelayCallback(kKey2, suspend_delay_callback2_);

  OnSuspendImminent(kSuspendId1);

  power_manager_.RemoveSuspendDelayCallback(kKey2);
  OnSuspendImminent(kSuspendId2);

  power_manager_.RemoveSuspendDelayCallback(kKey1);
  OnSuspendImminent(kSuspendId1);
}

typedef PowerManagerTest PowerManagerDeathTest;

TEST_F(PowerManagerDeathTest, AddStateChangeCallbackDuplicateKey) {
  power_manager_.AddStateChangeCallback(
      kKey1, Bind(&PowerManagerTest::StateChangeAction1, Unretained(this)));

#ifndef NDEBUG
  // Adding another callback with the same key is an error and causes a crash in
  // debug mode.
  EXPECT_DEATH(power_manager_.AddStateChangeCallback(
      kKey1, Bind(&PowerManagerTest::StateChangeAction2, Unretained(this))),
               "Inserting duplicate key");
#else  // NDEBUG
  EXPECT_CALL(*this, StateChangeAction2(PowerManagerProxyDelegate::kOn));
  power_manager_.AddStateChangeCallback(
      kKey1, Bind(&PowerManagerTest::StateChangeAction2, Unretained(this)));
  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kOn);
#endif  // NDEBUG
}

TEST_F(PowerManagerDeathTest, RemoveStateChangeCallbackUnknownKey) {
  power_manager_.AddStateChangeCallback(
      kKey1, Bind(&PowerManagerTest::StateChangeAction1, Unretained(this)));

#ifndef NDEBUG
  // Attempting to remove a callback key that was not added is an error and
  // crashes in debug mode.
  EXPECT_DEATH(power_manager_.RemoveStateChangeCallback(kKey2),
               "Removing unknown key");
#else  // NDEBUG
  EXPECT_CALL(*this, StateChangeAction1(PowerManagerProxyDelegate::kOn));

  // In non-debug mode, removing an unknown key does nothing.
  power_manager_.RemoveStateChangeCallback(kKey2);
  factory_.delegate()->OnPowerStateChanged(PowerManagerProxyDelegate::kOn);
#endif  // NDEBUG
}

TEST_F(PowerManagerTest, RegisterSuspendDelay) {
  const base::TimeDelta kTimeout = base::TimeDelta::FromMilliseconds(100);
  const char kDescription[] = "description";
  int delay_id = 0;
  EXPECT_CALL(*factory_.proxy(), RegisterSuspendDelay(kTimeout, kDescription,
                                                      &delay_id))
      .WillOnce(Return(true));
  EXPECT_TRUE(power_manager_.RegisterSuspendDelay(kTimeout, kDescription,
                                                  &delay_id));
}

TEST_F(PowerManagerTest, UnregisterSuspendDelay) {
  const int kDelayId = 123;
  EXPECT_CALL(*factory_.proxy(), UnregisterSuspendDelay(kDelayId))
      .WillOnce(Return(true));
  EXPECT_TRUE(power_manager_.UnregisterSuspendDelay(kDelayId));
}

TEST_F(PowerManagerTest, ReportSuspendReadiness) {
  const int kDelayId = 678;
  const int kSuspendId = 12345;
  EXPECT_CALL(*factory_.proxy(), ReportSuspendReadiness(kDelayId, kSuspendId))
      .WillOnce(Return(true));
  EXPECT_TRUE(power_manager_.ReportSuspendReadiness(kDelayId, kSuspendId));
}

TEST_F(PowerManagerTest, OnSuspendTimeout) {
  EXPECT_EQ(PowerManagerProxyDelegate::kUnknown, power_manager_.power_state());
  OnSuspendTimeout();
  EXPECT_EQ(PowerManagerProxyDelegate::kOn, power_manager_.power_state());
}

}  // namespace shill
