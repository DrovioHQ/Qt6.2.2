// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/file_system_access/mock_file_system_access_permission_grant.h"

namespace content {

MockFileSystemAccessPermissionGrant::MockFileSystemAccessPermissionGrant() =
    default;
MockFileSystemAccessPermissionGrant::~MockFileSystemAccessPermissionGrant() =
    default;

void MockFileSystemAccessPermissionGrant::RequestPermission(
    GlobalFrameRoutingId frame_id,
    UserActivationState user_activation_state,
    base::OnceCallback<void(PermissionRequestOutcome)> callback) {
  RequestPermission_(frame_id, user_activation_state, callback);
}

}  // namespace content
