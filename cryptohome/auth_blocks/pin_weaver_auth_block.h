// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"

#include "cryptohome/le_credential_manager.h"

#include <base/macros.h>

namespace cryptohome {

class PinWeaverAuthBlock : public SyncAuthBlock {
 public:
  explicit PinWeaverAuthBlock(LECredentialManager* le_manager,
                              CryptohomeKeysManager* cryptohome_keys_manager);
  PinWeaverAuthBlock(const PinWeaverAuthBlock&) = delete;
  PinWeaverAuthBlock& operator=(const PinWeaverAuthBlock&) = delete;

  CryptoError Create(const AuthInput& user_input,
                     AuthBlockState* auth_block_state,
                     KeyBlobs* key_blobs) override;

  CryptoError Derive(const AuthInput& auth_input,
                     const AuthBlockState& state,
                     KeyBlobs* key_blobs) override;

 private:
  // Handler for Low Entropy credentials.
  LECredentialManager* le_manager_;

  CryptohomeKeyLoader* cryptohome_key_loader_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_
