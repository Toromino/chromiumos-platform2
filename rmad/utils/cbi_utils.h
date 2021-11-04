// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CBI_UTILS_H_
#define RMAD_UTILS_CBI_UTILS_H_

#include <string>

namespace rmad {

class CbiUtils {
 public:
  CbiUtils() = default;
  virtual ~CbiUtils() = default;

  // Get the sku number of the device from cbi.
  virtual bool GetSku(uint64_t* sku) const = 0;

  // Get the dram part number of the device from cbi.
  virtual bool GetDramPartNum(std::string* dram_part_num) const = 0;

  // Set the sku number of the device to cbi.
  virtual bool SetSku(uint64_t sku) = 0;

  // Set the dram part number of the device to cbi.
  virtual bool SetDramPartNum(const std::string& dram_part_num) = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CBI_UTILS_H_
