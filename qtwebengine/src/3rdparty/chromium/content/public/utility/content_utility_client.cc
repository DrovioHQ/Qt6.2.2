// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/utility/content_utility_client.h"

namespace content {

bool ContentUtilityClient::OnMessageReceived(const IPC::Message& message) {
  return false;
}

bool ContentUtilityClient::HandleServiceRequestDeprecated(
    const std::string& service_name,
    mojo::ScopedMessagePipeHandle service_pipe) {
  return false;
}

}  // namespace content
