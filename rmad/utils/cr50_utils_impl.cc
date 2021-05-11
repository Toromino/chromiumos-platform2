// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/cr50_utils_impl.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_util.h>

namespace rmad {

constexpr char kGsctoolCmd[] = "gsctool";
const std::vector<std::string> kRsuArgv{kGsctoolCmd, "-a", "-r"};

bool Cr50UtilsImpl::RoVerificationKeyPressed() const {
  // TODO(b/181000999): Send a D-Bus query to tpm_managerd when API is ready.
  return false;
}

bool Cr50UtilsImpl::GetRsuChallengeCode(std::string* challenge_code) const {
  // TODO(chenghan): Check with cr50 team if we can expose a tpm_managerd API
  //                 for this, so we don't need to depend on `gsctool` output
  //                 format to do extra string parsing.
  if (base::GetAppOutput(kRsuArgv, challenge_code)) {
    base::RemoveChars(*challenge_code, base::kWhitespaceASCII, challenge_code);
    base::ReplaceFirstSubstringAfterOffset(challenge_code, 0, "Challenge:", "");
    LOG(INFO) << "Challenge code: " << *challenge_code;
    return true;
  }
  return false;
}

bool Cr50UtilsImpl::PerformRsu(const std::string& unlock_code) const {
  std::vector<std::string> argv(kRsuArgv);
  argv.push_back(unlock_code);
  std::string output;
  if (base::GetAppOutput(argv, &output)) {
    LOG(INFO) << "RSU succeeded.";
    return true;
  }
  LOG(INFO) << "RSU failed.";
  LOG(ERROR) << output;
  return false;
}

}  // namespace rmad
