// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/resources/resource_interface.h"

#include <utility>

#include <cstdint>

namespace reporting {

ScopedReservation::ScopedReservation(uint64_t size,
                                     ResourceInterface* resource_interface)
    : resource_interface_(resource_interface) {
  if (!resource_interface->Reserve(size)) {
    return;
  }
  size_ = size;
}

ScopedReservation::ScopedReservation(ScopedReservation&& other)
    : resource_interface_(other.resource_interface_),
      size_(std::move(other.size_)) {}

bool ScopedReservation::reserved() const {
  return size_.has_value();
}

bool ScopedReservation::Reduce(uint64_t new_size) {
  if (!reserved()) {
    return false;
  }
  if (new_size <= 0 || size_.value() < new_size) {
    return false;
  }
  resource_interface_->Discard(size_.value() - new_size);
  size_ = new_size;
  return true;
}

ScopedReservation::~ScopedReservation() {
  if (reserved()) {
    resource_interface_->Discard(size_.value());
  }
}

}  // namespace reporting
