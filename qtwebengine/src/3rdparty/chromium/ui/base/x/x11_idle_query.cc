// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/x/x11_idle_query.h"

#include "ui/gfx/x/connection.h"
#include "ui/gfx/x/future.h"
#include "ui/gfx/x/screensaver.h"

namespace ui {

IdleQueryX11::IdleQueryX11() : connection_(x11::Connection::Get()) {
  // Let the server know the client version before making any requests.
  if (!connection_->GetXlibDisplay())
    return;
  connection_->screensaver().QueryVersion(
      {x11::ScreenSaver::major_version, x11::ScreenSaver::minor_version});
}

IdleQueryX11::~IdleQueryX11() = default;

int IdleQueryX11::IdleTime() {
  if (!connection_->GetXlibDisplay())
    return 0;
  if (auto reply = connection_->screensaver()
                       .QueryInfo(connection_->default_root())
                       .Sync()) {
    return reply->ms_since_user_input / 1000;
  }
  return 0;
}

}  // namespace ui
