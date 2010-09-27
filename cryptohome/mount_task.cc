// Copyright (c) 2009 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mount_task.h"

namespace cryptohome {

base::AtomicSequenceNumber MountTask::sequence_holder_;

MountTask::MountTask(MountTaskObserver* observer,
                     Mount* mount,
                     const UsernamePasskey& credentials)
    : mount_(mount),
      credentials_(),
      sequence_id_(-1),
      observer_(observer),
      default_result_(new MountTaskResult),
      result_(default_result_.get()),
      complete_event_(NULL) {
  credentials_.Assign(credentials);
  sequence_id_ = NextSequence();
  result_->set_sequence_id(sequence_id_);
}

MountTask::~MountTask() {
}

int MountTask::NextSequence() {
  // AtomicSequenceNumber is zero-based, so increment so that the sequence ids
  // are one-based.
  return sequence_holder_.GetNext() + 1;
}

void MountTask::Notify() {
  if (observer_) {
    observer_->MountTaskObserve(*result_);
  }
  Signal();
}

void MountTask::Signal()
{
  if (complete_event_) {
    complete_event_->Signal();
  }
}

void MountTaskMount::Run() {
  if (mount_) {
    Mount::MountError code = Mount::MOUNT_ERROR_NONE;
    bool status = mount_->MountCryptohome(credentials_,
                                          mount_args_,
                                          &code);
    result()->set_return_status(status);
    result()->set_return_code(code);
  }
  MountTask::Notify();
}

void MountTaskMountGuest::Run() {
  if (mount_) {
    bool status = mount_->MountGuestCryptohome();
    result()->set_return_status(status);
  }
  MountTask::Notify();
}

void MountTaskMigratePasskey::Run() {
  if (mount_) {
    bool status = mount_->MigratePasskey(
        credentials_,
        static_cast<const char*>(old_key_.const_data()));
    result()->set_return_status(status);
  }
  MountTask::Notify();
}

void MountTaskUnmount::Run() {
  if (mount_) {
    bool status = mount_->UnmountCryptohome();
    result()->set_return_status(status);
  }
  MountTask::Notify();
}

void MountTaskTestCredentials::Run() {
  if (mount_) {
    bool status = mount_->TestCredentials(credentials_);
    result()->set_return_status(status);
  }
  MountTask::Notify();
}

void MountTaskRemove::Run() {
  if (mount_) {
    bool status = mount_->RemoveCryptohome(credentials_);
    result()->set_return_status(status);
  }
  MountTask::Notify();
}

void MountTaskResetTpmContext::Run() {
  if (mount_) {
    Crypto* crypto = mount_->get_crypto();
    if (crypto) {
      crypto->EnsureTpm(true);
    }
  }
  MountTask::Notify();
}

void MountTaskRemoveTrackedSubdirectories::Run() {
  result()->set_return_status(false);
  if (mount_) {
  }
  MountTask::Notify();
}

}  // namespace cryptohome
