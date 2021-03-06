// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/common/origin_util.h"

#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/stl_util.h"
#include "base/strings/pattern.h"
#include "content/common/url_schemes.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace content {

bool OriginCanAccessServiceWorkers(const GURL& url) {
  if (url.SchemeIsHTTPOrHTTPS() && network::IsUrlPotentiallyTrustworthy(url))
    return true;

  if (base::Contains(GetServiceWorkerSchemes(), url.scheme())) {
    return true;
  }

  return false;
}

}  // namespace content
