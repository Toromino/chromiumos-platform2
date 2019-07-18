// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/technology.h"

#include <set>
#include <string>
#include <vector>

#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/error.h"
#include "shill/logging.h"

namespace shill {

using std::set;
using std::string;
using std::vector;

namespace {

constexpr char kLoopbackName[] = "loopback";
constexpr char kTunnelName[] = "tunnel";
constexpr char kPPPName[] = "ppp";
constexpr char kUnknownName[] = "unknown";

}  // namespace

// static
bool GetTechnologyVectorFromString(const string& technologies_string,
                                   vector<Technology>* technologies_vector,
                                   Error* error) {
  CHECK(technologies_vector);
  CHECK(error);

  technologies_vector->clear();

  // Check if |technologies_string| is empty as some versions of
  // base::SplitString return a vector with one empty string when given an
  // empty string.
  if (technologies_string.empty()) {
    return true;
  }

  set<Technology> seen;
  vector<string> technology_parts = base::SplitString(
      technologies_string, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto& name : technology_parts) {
    Technology technology = Technology::CreateFromName(name);

    if (technology == Technology::kUnknown) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            name + " is an unknown technology name");
      return false;
    }

    if (base::ContainsKey(seen, technology)) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            name + " is duplicated in the list");
      return false;
    }
    seen.insert(technology);
    technologies_vector->push_back(technology);
  }

  return true;
}

// static
Technology Technology::CreateFromName(const string& name) {
  if (name == kTypeEthernet) {
    return kEthernet;
  } else if (name == kTypeEthernetEap) {
    return kEthernetEap;
  } else if (name == kTypeWifi) {
    return kWifi;
  } else if (name == kTypeCellular) {
    return kCellular;
  } else if (name == kTypeVPN) {
    return kVPN;
  } else if (name == kTypePPPoE) {
    return kPPPoE;
  } else if (name == kLoopbackName) {
    return kLoopback;
  } else if (name == kTunnelName) {
    return kTunnel;
  } else if (name == kPPPName) {
    return kPPP;
  } else {
    return kUnknown;
  }
}

// static
Technology Technology::CreateFromStorageGroup(const string& group) {
  vector<string> group_parts = base::SplitString(
      group, "_", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (group_parts.empty()) {
    return kUnknown;
  }
  return CreateFromName(group_parts[0]);
}

string Technology::GetName() const {
  if (type_ == kEthernet) {
    return kTypeEthernet;
  } else if (type_ == kEthernetEap) {
    return kTypeEthernetEap;
  } else if (type_ == kWifi) {
    return kTypeWifi;
  } else if (type_ == kCellular) {
    return kTypeCellular;
  } else if (type_ == kVPN) {
    return kTypeVPN;
  } else if (type_ == kLoopback) {
    return kLoopbackName;
  } else if (type_ == kTunnel) {
    return kTunnelName;
  } else if (type_ == kPPP) {
    return kPPPName;
  } else if (type_ == kPPPoE) {
    return kTypePPPoE;
  } else {
    return kUnknownName;
  }
}

bool Technology::IsPrimaryConnectivityTechnology() const {
  return (type_ == kCellular || type_ == kEthernet || type_ == kWifi ||
          type_ == kPPPoE);
}

std::ostream& operator<<(std::ostream& os, const Technology& technology) {
  os << technology.GetName();
  return os;
}

}  // namespace shill
