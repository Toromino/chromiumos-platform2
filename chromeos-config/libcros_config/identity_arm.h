// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Reads ARM identity information and checks for device compatibility.

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_ARM_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_ARM_H_

#include <string>

#include "chromeos-config/libcros_config/identity.h"

namespace base {
class DictionaryValue;
class FilePath;
}  // namespace base

namespace brillo {

class CrosConfigIdentityArm : public CrosConfigIdentity {
 public:
  CrosConfigIdentityArm();
  ~CrosConfigIdentityArm();

  // Read the compatible devices list from the device-tree compatible file.
  //
  // @dt_compatible_file: File to read - typically /proc/device-tree/compatible
  // @sku_id_file: File containing SKU ID integer
  bool ReadInfo(const base::FilePath& dt_compatible_file,
                const base::FilePath& sku_id_file);

  // Write out fake device-tree compatible file for testing purposes.
  // @device_name: Device name to write to the compatible file
  // @sku_id_file: File containing SKU ID integer
  // @dt_compatible_file_out: Returns the file that was written
  // @sku_id_file_out: File that the SKU ID integer was written into
  // @return true if OK, false on error
  bool FakeProductFilesForTesting(const std::string& device_name,
                                  const int sku_id,
                                  base::FilePath* dt_compatible_file_out,
                                  base::FilePath* sku_id_file_out);

  // Checks if the device_name exists in the compatible devices string.
  // @return true if device is compatible
  bool IsCompatible(const std::string& device_name) const;

  // @return Compatible devices string read via ReadDtCompatible
  const std::string& GetCompatibleDeviceString() const {
    return compatible_devices_;
  }

  // CrosConfigIdentity:
  // Check that the identity is device-tree compatible with the one
  // specified in the identity dictionary
  bool PlatformIdentityMatch(
      const base::DictionaryValue& identity_dict) const override;

  std::string DebugString() const override;

 private:
  std::string compatible_devices_;

  DISALLOW_COPY_AND_ASSIGN(CrosConfigIdentityArm);
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_ARM_H_
