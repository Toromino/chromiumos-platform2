// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_ROLLBACK_CONSTANTS_H_
#define OOBE_CONFIG_ROLLBACK_CONSTANTS_H_

#include <base/files/file_util.h>

namespace oobe_config {

// /mnt/stateful_partition/
extern const base::FilePath kStatefulPartition;

// oobe_config_prepare_save copied the files into this directory.
extern const base::FilePath kSaveTempPath;
// oobe_config_finish_restore is expecting the files in this directory.
extern const base::FilePath kRestoreTempPath;
// The rollback data is stored here on unencrypted stateful as an encrypted
// proto. This is the file which is preserved over powerwash.
extern const base::FilePath kUnencryptedStatefulRollbackDataPath;
// The rollback data is stored here on encrypted stateful as an unencrypted
// proto.
extern const base::FilePath kEncryptedStatefulRollbackDataPath;

// The name of the marker file used to trigger a save of rollback data
// during the next shutdown.
extern const base::FilePath kRollbackSaveMarkerFile;

// The path to the file that indicates if OOBE has completed.
extern const base::FilePath kOobeCompletedFile;
extern const char kOobeCompletedFileName[];

// The path to the file that indicates if metrics reporting is enabled.
extern const base::FilePath kMetricsReportingEnabledFile;
extern const char kMetricsReportingEnabledFileName[];

extern const char kOobeConfigSaveUsername[];
extern const char kRootUsername[];
extern const char kPreserveGroupName[];

// Path to the file indicating the data save was successful.
extern const base::FilePath kDataSavedFile;

extern const base::FilePath kRollbackDataForPmsgFile;
extern const base::FilePath kPstorePath;

}  // namespace oobe_config

#endif  // OOBE_CONFIG_ROLLBACK_CONSTANTS_H_
