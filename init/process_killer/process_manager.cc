// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/process_killer/process_manager.h"

#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <init/process_killer/process.h>

namespace {
constexpr char kCommPath[] = "comm";
constexpr char kMountInfoPath[] = "mountinfo";
constexpr char kFDPath[] = "fd";
}  // namespace

namespace init {

ProcessManager::ProcessManager(const base::FilePath& proc) : proc_path_(proc) {}

std::vector<ActiveMount> ProcessManager::GetMountsForProcess(pid_t pid) {
  base::FilePath mounts_for_process =
      proc_path_.AppendASCII(base::NumberToString(pid))
          .AppendASCII(kMountInfoPath);
  std::string mount_info;
  std::vector<ActiveMount> ret;

  if (!base::ReadFileToString(mounts_for_process, &mount_info)) {
    PLOG_IF(ERROR, errno != ENOENT) << "Failed to read mount info";
    return ret;
  }
  auto mounts = base::SplitStringPiece(mount_info, "\n", base::KEEP_WHITESPACE,
                                       base::SPLIT_WANT_NONEMPTY);

  for (auto& mount : mounts) {
    auto args = base::SplitStringPiece(mount, " ", base::TRIM_WHITESPACE,
                                       base::SPLIT_WANT_NONEMPTY);

    ActiveMount parsed_mount;
    parsed_mount.source = base::FilePath(args[3]);
    parsed_mount.target = base::FilePath(args[4]);
    parsed_mount.device = std::string(args[8]);

    ret.push_back(parsed_mount);
  }
  return ret;
}

std::vector<OpenFileDescriptor> ProcessManager::GetFileDescriptorsForProcess(
    pid_t pid) {
  std::vector<OpenFileDescriptor> ret;
  base::FilePath fdinfo_path = base::FilePath(
      base::StringPrintf("%s/%d/%s", proc_path_.value().c_str(), pid, kFDPath));

  base::FileEnumerator enumerator(
      fdinfo_path, false /* recursive */,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);

  for (base::FilePath fd = enumerator.Next(); !fd.empty();
       fd = enumerator.Next()) {
    // stat() the file descriptor to get the path of the link and the device id
    // for the device this file belongs on. Additionally, only consider symlinks
    // and ignore everything else.
    struct stat statbuf;
    if (lstat(fd.value().c_str(), &statbuf) || !S_ISLNK(statbuf.st_mode))
      continue;

    OpenFileDescriptor fd_target;

    if (!base::ReadSymbolicLink(fd, &fd_target.path)) {
      PLOG_IF(ERROR, errno != ENOENT)
          << "Failed to read link " << fd_target.path.value();
      continue;
    }
    ret.push_back(fd_target);
  }

  return ret;
}

std::vector<ActiveProcess> ProcessManager::GetProcessList() {
  std::vector<ActiveProcess> process_list;

  base::FileEnumerator dir_enum(proc_path_, false /* recursive */,
                                base::FileEnumerator::DIRECTORIES);
  for (base::FilePath cur_dir = dir_enum.Next(); !cur_dir.empty();
       cur_dir = dir_enum.Next()) {
    uint64_t pid64;

    // Ignore non-numeric directories.
    if (!base::StringToUint64(cur_dir.BaseName().value(), &pid64))
      continue;

    pid_t pid = base::checked_cast<pid_t>(pid64);
    auto mounts = GetMountsForProcess(pid);
    auto fds = GetFileDescriptorsForProcess(pid);

    base::FilePath comm_path = base::FilePath(base::StringPrintf(
        "%s/%d/%s", proc_path_.value().c_str(), pid, kCommPath));

    std::string comm;
    if (!base::ReadFileToString(comm_path, &comm)) {
      PLOG_IF(ERROR, errno != ENOENT) << "Failed to read comm for process";
      continue;
    }

    process_list.push_back(ActiveProcess(pid, comm, mounts, fds));
  }

  return process_list;
}

bool ProcessManager::SendSignalToProcess(const ActiveProcess& p, int signal) {
  return kill(p.GetPid(), signal) == 0;
}

}  // namespace init
