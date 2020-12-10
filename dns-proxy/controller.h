// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_CONTROLLER_H_
#define DNS_PROXY_CONTROLLER_H_

#include <iostream>
#include <memory>
#include <set>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process/process_reaper.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <shill/dbus/client/client.h>

#include "dns-proxy/proxy.h"

namespace dns_proxy {

// The parent process for the service. Responsible for managing the proxy
// subprocesses.
class Controller : public brillo::DBusDaemon {
 public:
  explicit Controller(const std::string& progname);
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;
  ~Controller();

 protected:
  int OnInit() override;
  void OnShutdown(int*) override;

 private:
  struct ProxyProc {
    ProxyProc() : pid(0) {}
    ProxyProc(Proxy::Type type, const std::string& ifname) : pid(0) {
      opts.type = type;
      opts.ifname = ifname;
    }

    friend std::ostream& operator<<(std::ostream& stream,
                                    const Controller::ProxyProc& proc) {
      stream << proc.opts;
      if (proc.pid > 0) {
        stream << "(pid: " << proc.pid << ")";
      }
      return stream;
    }

    // |pid| is intentionally excluded as only the strings are used as a key.
    bool operator<(const ProxyProc& that) const {
      return (opts.type < that.opts.type || opts.ifname < that.opts.ifname);
    }

    pid_t pid;
    Proxy::Options opts;
  };

  // This helper class keeps track of the dependencies for which the default
  // network proxy is required to run - namely whenever a VPN is connected or at
  // least one single-networked guest is running.
  class DefaultProxyDeps {
   public:
    explicit DefaultProxyDeps(base::RepeatingCallback<void(bool)> eval)
        : vpn_(false), eval_(eval) {}

    void vpn_on(bool b) {
      vpn_ = b;
      eval();
    }
    void guest_up(const std::string& s) {
      guests_.insert(s);
      eval();
    }
    void guest_down(const std::string& s) {
      guests_.erase(s);
      eval();
    }

   private:
    void eval() { eval_.Run(vpn_ || !guests_.empty()); }

    bool vpn_{false};
    std::set<std::string> guests_;
    base::RepeatingCallback<void(bool)> eval_;
  };

  void Setup();
  void OnPatchpanelReady(bool success);

  void RunProxy(Proxy::Type type, const std::string& ifname = "");
  void KillProxy(Proxy::Type type, const std::string& ifname = "");
  void Kill(const ProxyProc& proc);
  void OnProxyExit(pid_t pid, const siginfo_t& siginfo);

  // Callback used to run/kill default proxy based on its dependencies.
  // |has_deps| will be true if either VPN or a single-networked guest OS is
  // running.
  void EvalDefaultProxyDeps(bool has_deps);

  // Notified by shill whenever the device service changes.
  void OnDefaultServiceChanged(const std::string& type);

  // Notified by patchpanel whenever a change occurs in one of its virtual
  // network devices.
  void OnVirtualDeviceChanged(
      const patchpanel::NetworkDeviceChangedSignal& signal);
  void VirtualDeviceAdded(const patchpanel::NetworkDevice& device);
  void VirtualDeviceRemoved(const patchpanel::NetworkDevice& device);

  const std::string progname_;
  brillo::ProcessReaper process_reaper_;
  std::set<ProxyProc> proxies_;
  std::unique_ptr<DefaultProxyDeps> default_proxy_deps_;

  std::unique_ptr<shill::Client> shill_;
  std::unique_ptr<patchpanel::Client> patchpanel_;

  base::WeakPtrFactory<Controller> weak_factory_{this};
};

}  // namespace dns_proxy

#endif  // DNS_PROXY_CONTROLLER_H_
