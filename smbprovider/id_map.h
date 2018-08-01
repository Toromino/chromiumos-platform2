// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_ID_MAP_H_
#define SMBPROVIDER_ID_MAP_H_

#include <unordered_map>
#include <utility>

#include <base/logging.h>
#include <base/macros.h>

namespace smbprovider {

// Class that maps an int32_t ID to another type. Each new ID is not currently
// in use, but IDs can be reused after that item is removed from the map.
// Primarily used for handing out pseudo file descriptors.
template <typename T>
class IdMap {
 public:
  using MapType = std::unordered_map<int32_t, T>;

  IdMap() = default;
  ~IdMap() = default;

  int32_t Insert(T value) {
    const int32_t next_id = GetNextId();
    DCHECK_EQ(0, ids_.count(next_id));

    ids_.insert({next_id, std::move(value)});
    return next_id;
  }

  typename MapType::const_iterator Find(int32_t id) const {
    return ids_.find(id);
  }

  bool Contains(int32_t id) const { return ids_.count(id) > 0; }

  bool Remove(int32_t id) { return ids_.erase(id) > 0; }

  size_t Count() const { return ids_.size(); }

  typename MapType::const_iterator End() const { return ids_.end(); }

 private:
  // Returns the next ID and updates the internal state to ensure that
  // an ID that is already in use is not returned.
  int32_t GetNextId() { return next_id_++; }

  MapType ids_;
  int32_t next_id_ = 0;
  DISALLOW_COPY_AND_ASSIGN(IdMap);
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_ID_MAP_H_
