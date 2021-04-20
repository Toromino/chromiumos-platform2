// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/metrics.h"

namespace {
constexpr char kPartnerTypeMetricName[] = "ChromeOS.TypeC.PartnerType";
constexpr char kCableSpeedMetricName[] = "ChromeOS.TypeC.CableSpeed";
}  // namespace

namespace typecd {

void Metrics::ReportPartnerType(PartnerTypeMetric type) {
  if (!metrics_library_.SendEnumToUMA(
          kPartnerTypeMetricName, static_cast<int>(type),
          static_cast<int>(PartnerTypeMetric::kMaxValue) + 1)) {
    LOG(WARNING) << "Failed to send partner type sample to UMA, type: "
                 << static_cast<int>(type);
  }
}

void Metrics::ReportCableSpeed(CableSpeedMetric speed) {
  if (!metrics_library_.SendEnumToUMA(
          kCableSpeedMetricName, static_cast<int>(speed),
          static_cast<int>(CableSpeedMetric::kMaxValue) + 1)) {
    LOG(WARNING) << "Failed to send cable speed sample to UMA, speed: "
                 << static_cast<int>(speed);
  }
}

}  // namespace typecd