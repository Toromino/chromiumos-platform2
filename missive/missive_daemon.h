// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_DAEMON_H_
#define MISSIVE_MISSIVE_DAEMON_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/threading/thread.h>
#include <brillo/daemons/dbus_daemon.h>

#include "protos/test.pb.h"

#include "dbus_adaptors/org.chromium.Missived.h"

namespace reporting {

class MissiveDaemon : public brillo::DBusServiceDaemon,
                      public org::chromium::MissivedAdaptor,
                      public org::chromium::MissivedInterface {
 public:
  MissiveDaemon();
  MissiveDaemon(const MissiveDaemon&) = delete;
  MissiveDaemon& operator=(const MissiveDaemon&) = delete;
  virtual ~MissiveDaemon() = default;

 private:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Forward org::chromium::MissivedInterface
  void EnqueueRecords(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                          reporting::test::TestMessage>> response,
                      const reporting::test::TestMessage& in_request) override;
  void FlushPriority(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         reporting::test::TestMessage>> response,
                     const reporting::test::TestMessage& in_request) override;
  void ConfirmRecordUpload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          reporting::test::TestMessage>> response,
      const reporting::test::TestMessage& in_request) override;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<MissiveDaemon> weak_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_DAEMON_H_