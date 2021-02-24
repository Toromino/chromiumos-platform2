// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session.h"

#include <string>
#include <utility>

#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

// Size of the values used serialization of UnguessableToken.
constexpr int kSizeOfSerializedValueInToken = sizeof(uint64_t);
// Number of uint64 used serialization of UnguessableToken.
constexpr int kNumberOfSerializedValuesInToken = 2;
// Offset where the high value is used in Serialized string.
constexpr int kHighTokenOffset = 0;
// Offset where the low value is used in Serialized string.
constexpr int kLowTokenOffset = kSizeOfSerializedValueInToken;
// AuthSession will time out if it is active after this time interval.
constexpr base::TimeDelta kAuthSessionTimeoutInMinutes =
    base::TimeDelta::FromMinutes(5);

AuthSession::AuthSession(
    std::string username,
    base::OnceCallback<void(const base::UnguessableToken&)> on_timeout,
    KeysetManagement* keyset_management)
    : username_(username),
      on_timeout_(std::move(on_timeout)),
      keyset_management_(keyset_management) {
  token_ = base::UnguessableToken::Create();
  timer_.Start(
      FROM_HERE, kAuthSessionTimeoutInMinutes,
      base::Bind(&AuthSession::AuthSessionTimedOut, base::Unretained(this)));
}

AuthSession::~AuthSession() = default;

void AuthSession::AuthSessionTimedOut() {
  status_ = AuthStatus::kAuthStatusTimedOut;
  // After this call back to |UserDataAuth|, |this| object will be deleted.
  std::move(on_timeout_).Run(token_);
}

user_data_auth::CryptohomeErrorCode AuthSession::Authenticate(
    const cryptohome::AuthorizationRequest& authorization_request) {
  auto credentials = std::make_unique<Credentials>(
      username_, brillo::SecureBlob(authorization_request.key().secret()));
  credentials->set_key_data(authorization_request.key().data());

  MountError code;
  std::unique_ptr<VaultKeyset> vault_keyset =
      keyset_management_->LoadUnwrappedKeyset(*credentials, &code);
  if (vault_keyset && code == MOUNT_ERROR_NONE) {
    file_system_keyset_ = std::make_unique<FileSystemKeyset>(*vault_keyset);
    status_ = AuthStatus::kAuthStatusAuthenticated;
  }
  return MountErrorToCryptohomeError(code);
}

// static
base::Optional<std::string> AuthSession::GetSerializedStringFromToken(
    const base::UnguessableToken& token) {
  if (token == base::UnguessableToken::Null()) {
    LOG(ERROR) << "Invalid UnguessableToken given";
    return base::nullopt;
  }
  std::string serialized_token;
  serialized_token.resize(kSizeOfSerializedValueInToken *
                          kNumberOfSerializedValuesInToken);
  uint64_t high = token.GetHighForSerialization();
  uint64_t low = token.GetLowForSerialization();
  memcpy(&serialized_token[kHighTokenOffset], &high, sizeof(high));
  memcpy(&serialized_token[kLowTokenOffset], &low, sizeof(low));
  return serialized_token;
}

// static
base::Optional<base::UnguessableToken>
AuthSession::GetTokenFromSerializedString(const std::string& serialized_token) {
  if (serialized_token.size() !=
      kSizeOfSerializedValueInToken * kNumberOfSerializedValuesInToken) {
    LOG(ERROR) << "Incorrect serialized string size";
    return base::nullopt;
  }
  uint64_t high, low;
  memcpy(&high, &serialized_token[kHighTokenOffset], sizeof(high));
  memcpy(&low, &serialized_token[kLowTokenOffset], sizeof(low));
  return base::UnguessableToken::Deserialize(high, low);
}

}  // namespace cryptohome