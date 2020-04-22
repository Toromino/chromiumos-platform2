// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/sandboxed_worker.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <brillo/http/http_transport.h>
#include <google/protobuf/repeated_field.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/system_proxy_adaptor.h"

namespace {
constexpr char kSystemProxyWorkerBin[] = "/usr/sbin/system_proxy_worker";
constexpr char kSeccompFilterPath[] =
    "/usr/share/policy/system-proxy-worker-seccomp.policy";
constexpr int kMaxWorkerMessageSize = 2048;
// Size of the buffer array used to read data from the worker's stderr.
constexpr int kWorkerBufferSize = 1024;
constexpr char kPrefixDirect[] = "direct://";
constexpr char kPrefixHttp[] = "http://";
}  // namespace

namespace system_proxy {

SandboxedWorker::SandboxedWorker(base::WeakPtr<SystemProxyAdaptor> adaptor)
    : jail_(minijail_new()), adaptor_(adaptor), pid_(0) {}

bool SandboxedWorker::Start() {
  DCHECK(!IsRunning()) << "Worker is already running.";

  if (!jail_)
    return false;

  minijail_namespace_pids(jail_.get());
  minijail_namespace_net(jail_.get());
  minijail_no_new_privs(jail_.get());
  minijail_use_seccomp_filter(jail_.get());
  minijail_parse_seccomp_filters(jail_.get(), kSeccompFilterPath);

  int child_stdin = -1, child_stdout = -1, child_stderr = -1;

  std::vector<char*> args_ptr;

  args_ptr.push_back(const_cast<char*>(kSystemProxyWorkerBin));
  args_ptr.push_back(nullptr);

  // Execute the command.
  int res =
      minijail_run_pid_pipes(jail_.get(), args_ptr[0], args_ptr.data(), &pid_,
                             &child_stdin, &child_stdout, &child_stderr);

  if (res != 0) {
    LOG(ERROR) << "Failed to start sandboxed worker: " << strerror(-res);
    return false;
  }

  // Make sure the pipes never block.
  if (!base::SetNonBlocking(child_stdin))
    LOG(WARNING) << "Failed to set stdin non-blocking";
  if (!base::SetNonBlocking(child_stdout))
    LOG(WARNING) << "Failed to set stdout non-blocking";
  if (!base::SetNonBlocking(child_stderr))
    LOG(WARNING) << "Failed to set stderr non-blocking";

  stdin_pipe_.reset(child_stdin);
  stdout_pipe_.reset(child_stdout);
  stderr_pipe_.reset(child_stderr);

  stdout_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      stdout_pipe_.get(),
      base::BindRepeating(&SandboxedWorker::OnMessageReceived,
                          base::Unretained(this)));

  stderr_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      stderr_pipe_.get(), base::BindRepeating(&SandboxedWorker::OnErrorReceived,
                                              base::Unretained(this)));
  return true;
}

void SandboxedWorker::SetUsernameAndPassword(const std::string& username,
                                             const std::string& password) {
  worker::Credentials credentials;
  credentials.set_username(username);
  credentials.set_password(password);
  worker::WorkerConfigs configs;
  *configs.mutable_credentials() = credentials;
  if (!WriteProtobuf(stdin_pipe_.get(), configs)) {
    LOG(ERROR) << "Failed to set credentials for worker " << pid_;
  }
}

bool SandboxedWorker::SetListeningAddress(uint32_t addr, int port) {
  worker::SocketAddress address;
  address.set_addr(addr);
  address.set_port(port);
  worker::WorkerConfigs configs;
  *configs.mutable_listening_address() = address;

  if (!WriteProtobuf(stdin_pipe_.get(), configs)) {
    LOG(ERROR) << "Failed to set local proxy address for worker " << pid_;
    return false;
  }
  return true;
}

bool SandboxedWorker::Stop() {
  if (is_being_terminated_)
    return true;
  LOG(INFO) << "Killing " << pid_;
  is_being_terminated_ = true;

  if (kill(pid_, SIGTERM) < 0) {
    if (errno == ESRCH) {
      // No process or group found for pid, assume already terminated.
      return true;
    }
    PLOG(ERROR) << "Failed to terminate process " << pid_;
    return false;
  }
  return true;
}

bool SandboxedWorker::IsRunning() {
  return pid_ != 0 && !is_being_terminated_;
}

void SandboxedWorker::OnMessageReceived() {
  worker::WorkerRequest request;

  if (!ReadProtobuf(stdout_pipe_.get(), &request)) {
    LOG(ERROR) << "Failed to read request from worker " << pid_;
    // The message is corrupted or the pipe closed, either way stop listening.
    stdout_watcher_ = nullptr;
    return;
  }
  if (request.has_log_request()) {
    LOG(INFO) << "[worker: " << pid_ << "]" << request.log_request().message();
  }

  if (request.has_proxy_resolution_request()) {
    const worker::ProxyResolutionRequest& proxy_request =
        request.proxy_resolution_request();

    // This callback will always be called with at least one proxy entry. Even
    // if the dbus call itself fails, the proxy server list will contain the
    // direct proxy.
    adaptor_->GetChromeProxyServersAsync(
        proxy_request.target_url(),
        base::BindRepeating(&SandboxedWorker::OnProxyResolved,
                            weak_ptr_factory_.GetWeakPtr(),
                            proxy_request.target_url()));
  }
}

void SandboxedWorker::SetNetNamespaceLifelineFd(
    base::ScopedFD net_namespace_lifeline_fd) {
  // Sanity check that only one network namespace is setup for the worker
  // process.
  DCHECK(!net_namespace_lifeline_fd.is_valid());
  net_namespace_lifeline_fd_ = std::move(net_namespace_lifeline_fd);
}

void SandboxedWorker::OnErrorReceived() {
  std::vector<char> buf;
  buf.resize(kWorkerBufferSize);

  std::string message;
  std::string worker_msg = "[worker: " + std::to_string(pid_) + "] ";

  ssize_t count = kWorkerBufferSize;
  ssize_t total_count = 0;

  while (count == kWorkerBufferSize) {
    count = HANDLE_EINTR(read(stderr_pipe_.get(), buf.data(), buf.size()));

    if (count < 0) {
      PLOG(ERROR) << worker_msg << "Failed to read from stdio";
      return;
    }

    if (count == 0) {
      if (!message.empty())
        break;  // Full message was read at the first iteration.

      PLOG(INFO) << worker_msg << "Pipe closed";
      // Stop watching, otherwise the handler will fire forever.
      stderr_watcher_ = nullptr;
    }

    total_count += count;
    if (total_count > kMaxWorkerMessageSize) {
      LOG(ERROR) << "Failure to read message from woker: message size exceeds "
                    "maximum allowed";
      stderr_watcher_ = nullptr;
      return;
    }
    message.append(buf.begin(), buf.begin() + count);
  }

  LOG(ERROR) << worker_msg << message;
}

void SandboxedWorker::OnProxyResolved(
    const std::string& target_url,
    bool success,
    const std::vector<std::string>& proxy_servers) {
  worker::ProxyResolutionReply reply;
  reply.set_target_url(target_url);

  // Only http and direct proxies are supported at the moment.
  for (const auto& proxy : proxy_servers) {
    if (base::StartsWith(proxy, kPrefixHttp,
                         base::CompareCase::INSENSITIVE_ASCII) ||
        base::StartsWith(proxy, kPrefixDirect,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      reply.add_proxy_servers(proxy);
    }
  }

  worker::WorkerConfigs configs;
  *configs.mutable_proxy_resolution_reply() = reply;

  if (!WriteProtobuf(stdin_pipe_.get(), configs)) {
    LOG(ERROR) << "Failed to send proxy resolution reply to worker" << pid_;
  }
}

}  // namespace system_proxy
