// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/process.h"

#include <utility>

#include <signal.h>
#include <sysexits.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>

#include <base/logging.h>
#include <base/process/process_metrics.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <libminijail.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <scoped_minijail.h>

#include "ml/daemon.h"
#include "ml/machine_learning_service_impl.h"

namespace ml {

namespace {
constexpr char kMojoBootstrapFdSwitchName[] = "mojo-bootstrap-fd";

constexpr char kInternalMojoPrimordialPipeName[] = "cros_ml";

constexpr char kMlServiceBinaryPath[] = "/usr/bin/ml_service";

constexpr uid_t kMlServiceDBusUid = 20177;

std::string GetSeccompPolicyPath(const std::string& model_name) {
  return "/usr/share/policy/ml_service-" + model_name + "-seccomp.policy";
}

std::string GetArgumentForWorkerProcess(int fd) {
  std::string fd_argv = kMojoBootstrapFdSwitchName;
  return "--" + fd_argv + "=" + std::to_string(fd);
}

void InternalPrimordialMojoPipeDisconnectHandler(pid_t child_pid) {
  Process::GetInstance()->UnregisterWorkerProcess(child_pid);
  // Reap the worker process.
  int status;
  pid_t ret_pid = waitpid(child_pid, &status, 0);
  DCHECK(ret_pid == child_pid);
  // TODO(https://crbug.com/1202545): report WEXITSTATUS(status) to UMA.
  DVLOG(1) << "Worker process (" << child_pid << ") exits with status "
           << WEXITSTATUS(status);
}
}  // namespace

// static
Process* Process::GetInstance() {
  // This is thread-safe.
  static base::NoDestructor<Process> instance;
  return instance.get();
}

int Process::Run(int argc, char* argv[]) {
  // Parses the command line and determines the process type.
  base::CommandLine command_line(argc, argv);
  std::string mojo_fd_string =
      command_line.GetSwitchValueASCII(kMojoBootstrapFdSwitchName);

  if (mojo_fd_string.empty()) {
    process_type_ = Type::kControl;
  } else {
    process_type_ = Type::kWorker;
  }

  if (!command_line.GetArgs().empty()) {
    LOG(ERROR) << "Unexpected command line arguments: "
               << base::JoinString(command_line.GetArgs(), "\t");
    return ExitCode::kUnexpectedCommandLine;
  }

  if (process_type_ == Type::kControl) {
    ControlProcessRun();
  } else {
    // The process type is either "control" or "worker".
    DCHECK(GetType() == Type::kWorker);
    const auto is_valid_fd_str =
        base::StringToInt(mojo_fd_string, &mojo_bootstrap_fd_);
    DCHECK(is_valid_fd_str) << "Invalid mojo bootstrap fd";
    WorkerProcessRun();
  }

  return ExitCode::kSuccess;
}

Process::Type Process::GetType() {
  return process_type_;
}

bool Process::SpawnWorkerProcessAndGetPid(const mojo::PlatformChannel& channel,
                                          const std::string& model_name,
                                          pid_t* worker_pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(worker_pid != nullptr);
  // Should only be called by the control process.
  DCHECK(process_type_ == Type::kControl)
      << "Should only be called by the control process";

  // Start the process.
  ScopedMinijail jail(minijail_new());

  minijail_namespace_ipc(jail.get());
  minijail_namespace_uts(jail.get());
  minijail_namespace_net(jail.get());
  minijail_namespace_cgroups(jail.get());
  minijail_namespace_pids(jail.get());
  minijail_namespace_vfs(jail.get());

  std::string seccomp_policy_path = GetSeccompPolicyPath(model_name);
  minijail_parse_seccomp_filters(jail.get(), seccomp_policy_path.c_str());
  minijail_use_seccomp_filter(jail.get());

  std::string fd_argv = kMojoBootstrapFdSwitchName;
  // Use GetFD instead of TakeFD to non-destructively obtain the fd.
  fd_argv = GetArgumentForWorkerProcess(
      channel.remote_endpoint().platform_handle().GetFD().get());

  std::string mlservice_binary_path(kMlServiceBinaryPath);

  char* const argv[3] = {&mlservice_binary_path[0], &fd_argv[0], nullptr};

  // TODO(https://crbug.com/1202545): report the failure.
  if (minijail_run_pid(jail.get(), kMlServiceBinaryPath, argv, worker_pid) !=
      0) {
    LOG(DFATAL) << "Failed to spawn worker process for " << model_name;
    return false;
  }

  return true;
}

mojo::Remote<chromeos::machine_learning::mojom::MachineLearningService>&
Process::SendMojoInvitationAndGetRemote(pid_t worker_pid,
                                        mojo::PlatformChannel channel,
                                        const std::string& model_name) {
  // Send the Mojo invitation to the worker process.
  mojo::OutgoingInvitation invitation;
  mojo::ScopedMessagePipeHandle pipe =
      invitation.AttachMessagePipe(kInternalMojoPrimordialPipeName);

  mojo::Remote<chromeos::machine_learning::mojom::MachineLearningService>
      remote(mojo::PendingRemote<
             chromeos::machine_learning::mojom::MachineLearningService>(
          std::move(pipe), 0u /* version */));

  mojo::OutgoingInvitation::Send(std::move(invitation), worker_pid,
                                 channel.TakeLocalEndpoint());

  remote.set_disconnect_handler(
      base::BindOnce(InternalPrimordialMojoPipeDisconnectHandler, worker_pid));

  DCHECK(worker_pid_info_map_.find(worker_pid) == worker_pid_info_map_.end())
      << "Worker pid already exists";

  WorkerInfo worker_info;
  worker_info.remote = std::move(remote);
  worker_info.process_metrics =
      base::ProcessMetrics::CreateProcessMetrics(worker_pid);
  // Baseline the CPU usage counter in `process_metrics` to be zero as of now.
  worker_info.process_metrics->GetPlatformIndependentCPUUsage();

  worker_pid_info_map_[worker_pid] = std::move(worker_info);

  return worker_pid_info_map_[worker_pid].remote;
}

void Process::UnregisterWorkerProcess(pid_t pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto iter = worker_pid_info_map_.find(pid);
  DCHECK(iter != worker_pid_info_map_.end()) << "Pid is not registered";
  worker_pid_info_map_.erase(iter);
}

Process::Process() : process_type_(Type::kUnset), mojo_bootstrap_fd_(-1) {}
Process::~Process() = default;

void Process::ControlProcessRun() {
  // We need to set euid to kMlServiceDBusUid to bootstrap DBus. Otherwise, DBus
  // will block us because our euid inside of the userns is 0 but is 20106
  // outside of the userns.
  if (seteuid(kMlServiceDBusUid) != 0) {
    // TODO(https://crbug.com/1202545): report this error to UMA.
    LOG(ERROR) << "Unable to change effective uid to " << kMlServiceDBusUid;
    exit(EX_OSERR);
  }

  ml::Daemon daemon;
  daemon.Run();
}

void Process::WorkerProcessRun() {
  brillo::BaseMessageLoop message_loop;
  message_loop.SetAsCurrent();
  DETACH_FROM_SEQUENCE(sequence_checker_);

  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
          mojo::PlatformHandle(base::ScopedFD(mojo_bootstrap_fd_))));
  mojo::ScopedMessagePipeHandle pipe =
      invitation.ExtractMessagePipe(kInternalMojoPrimordialPipeName);
  // The worker process exits if it disconnects with the control process.
  // This can be important because in the control process's disconnect handler
  // function we will use waitpid to wait for this process to finish. So
  // the exit here will make sure that the waitpid in control process
  // won't hang.
  MachineLearningServiceImpl machine_learning_service_impl(
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::MachineLearningService>(
          std::move(pipe)),
      message_loop.QuitClosure());
  message_loop.Run();
}

const std::unordered_map<pid_t, Process::WorkerInfo>&
Process::GetWorkerPidInfoMap() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return worker_pid_info_map_;
}

}  // namespace ml