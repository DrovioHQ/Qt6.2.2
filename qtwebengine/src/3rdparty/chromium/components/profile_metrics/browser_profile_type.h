// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PROFILE_METRICS_BROWSER_PROFILE_TYPE_H_
#define COMPONENTS_PROFILE_METRICS_BROWSER_PROFILE_TYPE_H_

namespace profile_metrics {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// A Java counterpart will be generated for this enum.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.components.profile_metrics
enum class BrowserProfileType {
  kRegular = 0,
  kIncognito = 1,
  kGuest = 2,
  kSystem = 3,
  kOtherOffTheRecordProfile = 4,
  kEphemeralGuest = 5,
  kMaxValue = kEphemeralGuest,
};

}  // namespace profile_metrics

#endif  // COMPONENTS_PROFILE_METRICS_BROWSER_PROFILE_TYPE_H_