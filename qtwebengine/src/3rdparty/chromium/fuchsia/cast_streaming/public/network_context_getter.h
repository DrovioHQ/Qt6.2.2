// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUCHSIA_CAST_STREAMING_PUBLIC_NETWORK_CONTEXT_GETTER_H_
#define FUCHSIA_CAST_STREAMING_PUBLIC_NETWORK_CONTEXT_GETTER_H_

#include "base/callback.h"

namespace network {
namespace mojom {
class NetworkContext;
}  // namespace mojom
}  // namespace network

namespace cast_streaming {

using NetworkContextGetter =
    base::RepeatingCallback<network::mojom::NetworkContext*()>;

// Sets the NetworkContextGetter for embedders that use the Network Service.
// This must be called before any call to CastStreamingSession::Start() and must
// only be called once. If the NetworkContext crashes, any existing Cast
// Streaming Session will eventually terminate and call
// CastStreamingSession::OnSessionEnded().
void SetNetworkContextGetter(NetworkContextGetter getter);

}  // namespace cast_streaming

#endif  // FUCHSIA_CAST_STREAMING_PUBLIC_NETWORK_CONTEXT_GETTER_H_
