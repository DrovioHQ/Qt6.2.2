// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAFE_BROWSING_CORE_BROWSER_SAFE_BROWSING_TOKEN_FETCHER_H_
#define COMPONENTS_SAFE_BROWSING_CORE_BROWSER_SAFE_BROWSING_TOKEN_FETCHER_H_

#include "base/callback.h"

namespace safe_browsing {

constexpr char kAPIScope[] =
    "https://www.googleapis.com/auth/chrome-safe-browsing";

// This interface is used to fetch access tokens for communcations with Safe
// Browsing. It asynchronously returns an access token for the current account
// (as determined in concrete implementations), or the empty string if no access
// token is available (e.g., an error occurred).
// This must be run on the UI thread.
class SafeBrowsingTokenFetcher {
 public:
  using Callback = base::OnceCallback<void(const std::string& access_token)>;

  virtual ~SafeBrowsingTokenFetcher() = default;

  // Begin fetching a token for the account. The
  // result will be returned in |callback|. Must be called on the UI thread.
  virtual void Start(Callback callback) = 0;
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_CORE_BROWSER_SAFE_BROWSING_TOKEN_FETCHER_H_
