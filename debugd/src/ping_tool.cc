// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/ping_tool.h"

#include <string>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_id.h"
#include "debugd/src/variant_utils.h"

namespace debugd {

namespace {

const char kSetuidHack[] =
    "/usr/libexec/debugd/helpers/minijail-setuid-hack.sh";
const char kPing[] = "/bin/ping";
const char kPing6[] = "/bin/ping6";

const char kPingToolErrorString[] = "org.chromium.debugd.error.Ping";

}  // namespace

bool PingTool::Start(const base::ScopedFD& outfd,
                     const std::string& destination,
                     const brillo::VariantDictionary& options,
                     std::string* out_id,
                     brillo::ErrorPtr* error) {
  ProcessWithId* p =
      CreateProcess(true /* sandboxed */, false /* access_root_mount_ns */,
      {"-pvrl", "--profile=minimalistic-mountns", "--uts",
       "-k", "tmpfs,/run,tmpfs,MS_NODEV|MS_NOEXEC|MS_NOSUID,mode=755,size=10M",
       // A /run/shill bind mount is needed to access /etc/resolv.conf, which
       // is a symlink to /run/shill/resolv.conf.
       "-b", "/run/shill"});
  if (!p) {
    DEBUGD_ADD_ERROR(
        error, kPingToolErrorString, "Could not create ping process");
    return false;
  }

  p->AddArg(kSetuidHack);
  if (brillo::GetVariantValueOrDefault<bool>(options, "v6"))
    p->AddArg(kPing6);
  else
    p->AddArg(kPing);

  if (options.count("broadcast") == 1)
    p->AddArg("-b");
  if (!AddIntOption(p, options, "count", "-c", error))
    return false;
  if (!AddIntOption(p, options, "interval", "-i", error))
    return false;
  if (options.count("numeric") == 1)
    p->AddArg("-n");
  if (!AddIntOption(p, options, "packetsize", "-s", error))
    return false;
  if (!AddIntOption(p, options, "waittime", "-W", error))
    return false;

  auto interface = options.find("interface");
  if (interface != options.end()) {
    p->AddStringOption("-I", interface->second.Get<std::string>());
  }

  p->AddArg(destination);
  p->BindFd(outfd.get(), STDOUT_FILENO);
  p->BindFd(outfd.get(), STDERR_FILENO);
  LOG(INFO) << "ping: running process id: " << p->id();
  p->Start();
  *out_id = p->id();
  return true;
}

}  // namespace debugd
