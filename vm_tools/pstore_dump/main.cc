// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/common/pstore.h"
#include "vm_tools/pstore_dump/persistent_ram_buffer.h"

#include <base/files/file_path.h>
#include <brillo/flag_helper.h>

int main(int argc, char** argv) {
  DEFINE_string(file, "", "path to a .pstore file  (default: ARCVM's .pstore)");
  brillo::FlagHelper::Init(
      argc, argv,
      "A helper to read .pstore files generated by the ARCVM's guest kernel.");

  base::FilePath path;
  if (FLAGS_file.empty()) {
    path = base::FilePath(vm_tools::kArcVmPstorePath);
  } else {
    path = base::FilePath(FLAGS_file);
  }

  if (!vm_tools::pstore_dump::HandlePstore(path)) {
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}
