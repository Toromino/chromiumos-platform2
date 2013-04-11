// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This provides some utility functions to do with chaps isolate support.

#include "chaps/isolate.h"

#include <string>
#include <chromeos/secure_blob.h>

using std::string;
using chromeos::SecureBlob;

namespace chaps {

IsolateCredentialManager::IsolateCredentialManager() { }

IsolateCredentialManager::~IsolateCredentialManager() { }

bool IsolateCredentialManager::GetCurrentUserIsolateCredential(
    SecureBlob* isolate_credential) {
  // On Chrome OS always use the default isolate credential.
  *isolate_credential = GetDefaultIsolateCredential();
  return true;
}

bool IsolateCredentialManager::GetUserIsolateCredential(
    const string& user, SecureBlob* isolate_credential) {
  // On Chrome OS always use the default isolate credential.
  *isolate_credential = GetDefaultIsolateCredential();
  return true;
}

} // namespace chaps
