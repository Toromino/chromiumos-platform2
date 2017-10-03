// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_DAEMON_H_
#define CROS_DISKS_DAEMON_H_

#include <memory>

#include <base/macros.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process_reaper.h>

#include "cros-disks/archive_manager.h"
#include "cros-disks/cros_disks_server.h"
#include "cros-disks/device_ejector.h"
#include "cros-disks/device_event_moderator.h"
#include "cros-disks/disk_manager.h"
#include "cros-disks/format_manager.h"
#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"
#include "cros-disks/rename_manager.h"
#include "cros-disks/session_manager_proxy.h"

namespace cros_disks {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  // |has_session_manager| indicates whether the presence of a SessionManager is
  // expected.
  explicit Daemon(bool has_session_manager);

  ~Daemon() override;

 private:
  // brillo::DBusServiceDaemon overrides:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  void OnDeviceEvents();

  const bool has_session_manager_;
  Metrics metrics_;
  Platform platform_;
  brillo::ProcessReaper process_reaper_;
  DeviceEjector device_ejector_;
  ArchiveManager archive_manager_;
  DiskManager disk_manager_;
  FormatManager format_manager_;
  RenameManager rename_manager_;
  std::unique_ptr<DeviceEventModerator> event_moderator_;
  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;
  std::unique_ptr<CrosDisksServer> server_;
  brillo::MessageLoop::TaskId device_event_task_id_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_DAEMON_H_
