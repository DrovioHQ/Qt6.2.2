// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/security_interstitials/core/features.h"

namespace security_interstitials {

const base::Feature kInsecureFormSubmissionInterstitial{
    "InsecureFormSubmissionInterstitial", base::FEATURE_ENABLED_BY_DEFAULT};

const char kInsecureFormSubmissionInterstitialMode[] = "mode";
const char kInsecureFormSubmissionInterstitialModeIncludeAllRedirects[] =
    "include-redirects";
const char kInsecureFormSubmissionInterstitialModeNoRedirects[] =
    "no-redirects";

}  // namespace security_interstitials
