// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_NVME_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_NVME_DEVICE_ADAPTER_H_

#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"

namespace diagnostics {

// NVME-specific data retrieval module.
class NvmeDeviceAdapter : public StorageDeviceAdapter {
 public:
  explicit NvmeDeviceAdapter(const base::FilePath& dev_sys_path);
  NvmeDeviceAdapter(const NvmeDeviceAdapter&) = delete;
  NvmeDeviceAdapter(NvmeDeviceAdapter&&) = delete;
  NvmeDeviceAdapter& operator=(const NvmeDeviceAdapter&) = delete;
  NvmeDeviceAdapter& operator=(NvmeDeviceAdapter&&) = delete;
  ~NvmeDeviceAdapter() override = default;

  std::string GetDeviceName() const override;
  std::string GetModel() const override;

 private:
  const base::FilePath dev_sys_path_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_NVME_DEVICE_ADAPTER_H_
