// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/data-snapshotd/dbus_adaptor.h"

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/data_encoding.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/rsa_private_key.h>

#include "arc/data-snapshotd/file_utils.h"
#include "bootlockbox-client/bootlockbox/boot_lockbox_client.h"

namespace arc {
namespace data_snapshotd {

namespace {

// Snapshot paths:
constexpr char kCommonSnapshotPath[] =
    "/mnt/stateful_partition/unencrypted/arc-data-snapshot/";
constexpr char kLastSnapshotPath[] = "last";
constexpr char kPreviousSnapshotPath[] = "previous";
constexpr char kHomeRootDirectory[] = "/home/root";

// System salt local path should match the one in init/arc-data-snapshotd.conf.
constexpr char kSystemSaltPath[] = "/run/arc-data-snapshotd/salt";

}  // namespace

// BootLockbox snapshot keys:
const char kLastSnapshotPublicKey[] = "snapshot_public_key_last";
const char kPreviousSnapshotPublicKey[] = "snapshot_public_key_previous";
// Android data directory name:
const char kAndroidDataDirectory[] = "android-data";
const char kDataDirectory[] = "data";

DBusAdaptor::DBusAdaptor()
    : DBusAdaptor(base::FilePath(kCommonSnapshotPath),
                  base::FilePath(kHomeRootDirectory),
                  cryptohome::BootLockboxClient::CreateBootLockboxClient(),
                  "" /* system_salt */) {}

DBusAdaptor::~DBusAdaptor() = default;

// static
std::unique_ptr<DBusAdaptor> DBusAdaptor::CreateForTesting(
    const base::FilePath& snapshot_directory,
    const base::FilePath& home_root_directory,
    std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client,
    const std::string& system_salt) {
  return base::WrapUnique(
      new DBusAdaptor(snapshot_directory, home_root_directory,
                      std::move(boot_lockbox_client), system_salt));
}

void DBusAdaptor::RegisterAsync(
    const scoped_refptr<dbus::Bus>& bus,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus, GetObjectPath()),
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));
}

bool DBusAdaptor::GenerateKeyPair() {
  // TODO(b/160387490): Implement showing a spinner screen.
  std::string last_public_key_digest;
  // Try to move last snapshot to previous for consistency.
  if (base::PathExists(last_snapshot_directory_) &&
      boot_lockbox_client_->Read(kLastSnapshotPublicKey,
                                 &last_public_key_digest) &&
      !last_public_key_digest.empty()) {
    if (boot_lockbox_client_->Store(kPreviousSnapshotPublicKey,
                                    last_public_key_digest) &&
        ClearSnapshot(false /* last */) &&
        base::Move(last_snapshot_directory_, previous_snapshot_directory_)) {
      boot_lockbox_client_->Store(kLastSnapshotPublicKey, "");
    } else {
      LOG(ERROR) << "Failed to move last to previous snapshot.";
    }
  }
  // Clear last snapshot - a new one will be created soon.
  if (!ClearSnapshot(true /* last */))
    return false;

  // Generate a key pair.
  public_key_info_.clear();
  std::unique_ptr<crypto::RSAPrivateKey> generated_private_key(
      crypto::RSAPrivateKey::Create(4096));
  if (!generated_private_key) {
    LOG(ERROR) << "Failed to generate a key pair.";
    return false;
  }
  if (!generated_private_key->ExportPublicKey(&public_key_info_)) {
    LOG(ERROR) << "Failed to export public key";
    return false;
  }

  // Store a new public key digest.
  std::string encoded_digest = CalculateEncodedSha256Digest(public_key_info_);
  if (!boot_lockbox_client_->Store(kLastSnapshotPublicKey, encoded_digest)) {
    LOG(ERROR) << "Failed to store a public key in BootLockbox.";
    return false;
  }
  // Save private key for later usage.
  private_key_ = std::move(generated_private_key);
  return true;
}

bool DBusAdaptor::TakeSnapshot(const std::string& account_id) {
  if (!private_key_ || public_key_info_.empty()) {
    LOG(ERROR) << "Private or public key does not exist.";
    return false;
  }
  if (base::DirectoryExists(last_snapshot_directory_)) {
    LOG(ERROR) << "Snapshot directory already exists. Should be cleared first.";
    return false;
  }

  std::string userhash = brillo::cryptohome::home::SanitizeUserNameWithSalt(
      account_id, brillo::SecureBlob(system_salt_));
  base::FilePath android_data_dir = home_root_directory_.Append(userhash);
  if (!base::DirectoryExists(android_data_dir)) {
    LOG(ERROR) << "The snapshotting directory does not exist. "
               << android_data_dir.value();
    return false;
  }
  android_data_dir = android_data_dir.Append(kAndroidDataDirectory);

  if (!base::DirectoryExists(android_data_dir)) {
    LOG(ERROR) << "The snapshotting directory does not exist. "
               << android_data_dir.value();
    return false;
  }

  if (!CopySnapshotDirectory(android_data_dir, last_snapshot_directory_)) {
    LOG(ERROR) << "Failed to copy snapshot directory from "
               << android_data_dir.value() << " to "
               << last_snapshot_directory_.value();
    return false;
  }

  if (!base::DirectoryExists(last_snapshot_directory_)) {
    LOG(ERROR) << "The snapshot directory was not copied";
    return false;
  }

  // This callback will be executed or released before the end of this function.
  base::ScopedClosureRunner snapshot_clearer(
      base::BindOnce(base::IgnoreResult(&DBusAdaptor::ClearSnapshot),
                     base::Unretained(this), true /* last */));
  if (!StorePublicKey(last_snapshot_directory_, public_key_info_))
    return false;
  if (!StoreUserhash(last_snapshot_directory_, userhash))
    return false;
  std::vector<uint8_t> key_info;
  private_key_->ExportPrivateKey(&key_info);
  std::unique_ptr<crypto::RSAPrivateKey> key(
      crypto::RSAPrivateKey::CreateFromPrivateKeyInfo(key_info));
  if (!SignAndStoreHash(last_snapshot_directory_, key.get(),
                        inode_verification_enabled_)) {
    return false;
  }
  // Snapshot saved correctly, release closure without running it.
  ignore_result(snapshot_clearer.Release());

  // Dispose keys.
  private_key_.reset();
  public_key_info_.clear();
  return true;
}

bool DBusAdaptor::ClearSnapshot(bool last) {
  base::FilePath dir(last ? last_snapshot_directory_
                          : previous_snapshot_directory_);
  if (!base::DirectoryExists(dir)) {
    LOG(WARNING) << "Snapshot directory is already empty: " << dir.value();
    return true;
  }
  if (!base::DeletePathRecursively(dir)) {
    LOG(ERROR) << "Failed to delete snapshot directory: " << dir.value();
    return false;
  }
  return true;
}

void DBusAdaptor::LoadSnapshot(const std::string& account_id,
                               bool* last,
                               bool* success) {
  std::string userhash = brillo::cryptohome::home::SanitizeUserNameWithSalt(
      account_id, brillo::SecureBlob(system_salt_));
  if (!base::DirectoryExists(home_root_directory_.Append(userhash))) {
    LOG(ERROR) << "User directory does not exist for user " << account_id;
    *success = false;
    return;
  }
  base::FilePath android_data_dir =
      home_root_directory_.Append(userhash).Append(kAndroidDataDirectory);
  if (TryToLoadSnapshot(userhash, last_snapshot_directory_, android_data_dir,
                        kLastSnapshotPublicKey)) {
    *last = true;
    *success = true;
    return;
  }
  if (TryToLoadSnapshot(userhash, previous_snapshot_directory_,
                        android_data_dir, kPreviousSnapshotPublicKey)) {
    *last = false;
    *success = true;
    return;
  }
  *success = false;
}

bool DBusAdaptor::TryToLoadSnapshot(const std::string& userhash,
                                    const base::FilePath& snapshot_dir,
                                    const base::FilePath& android_data_dir,
                                    const std::string& boot_lockbox_key) {
  if (!base::DirectoryExists(snapshot_dir)) {
    LOG(ERROR) << "Snapshot directory " << snapshot_dir.value()
               << " does not exist.";
    return false;
  }

  std::string expected_public_key_digest;
  if (!boot_lockbox_client_->Read(boot_lockbox_key,
                                  &expected_public_key_digest) ||
      expected_public_key_digest.empty()) {
    LOG(ERROR) << "Failed to read a public key digest " << boot_lockbox_key
               << " from BootLockbox.";
    return false;
  }

  if (!VerifyHash(snapshot_dir, userhash, expected_public_key_digest,
                  inode_verification_enabled_)) {
    LOG(ERROR) << "Hash verification failed.";
    return false;
  }

  if (!base::DeletePathRecursively(android_data_dir.Append(kDataDirectory))) {
    LOG(ERROR) << "Failed to remove android-data directory.";
    return false;
  }
  if (!CopySnapshotDirectory(snapshot_dir.Append(kDataDirectory),
                             android_data_dir)) {
    LOG(ERROR) << "Failed to copy a snapshot directory.";
    return false;
  }
  return true;
}

DBusAdaptor::DBusAdaptor(
    const base::FilePath& snapshot_directory,
    const base::FilePath& home_root_directory,
    std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client,
    const std::string& system_salt)
    : org::chromium::ArcDataSnapshotdAdaptor(this),
      last_snapshot_directory_(snapshot_directory.Append(kLastSnapshotPath)),
      previous_snapshot_directory_(
          snapshot_directory.Append(kPreviousSnapshotPath)),
      home_root_directory_(home_root_directory),
      boot_lockbox_client_(std::move(boot_lockbox_client)),
      system_salt_(system_salt) {
  DCHECK(boot_lockbox_client_);
  if (system_salt_.empty() &&
      !base::ReadFileToString(base::FilePath(kSystemSaltPath), &system_salt_)) {
    LOG(ERROR) << "No available system salt.";
  }
}

}  // namespace data_snapshotd
}  // namespace arc