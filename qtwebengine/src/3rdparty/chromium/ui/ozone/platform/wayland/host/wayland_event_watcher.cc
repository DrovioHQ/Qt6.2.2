// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_event_watcher.h"

#include <cstring>

#include "base/bind.h"
#include "base/check.h"
#include "base/logging.h"
#include "base/task/current_thread.h"
#include "ui/events/event.h"
#include "ui/ozone/platform/wayland/common/wayland.h"

namespace ui {

WaylandEventWatcher::WaylandEventWatcher(wl_display* display)
    : controller_(FROM_HERE), display_(display) {
  DCHECK(display_);
}

WaylandEventWatcher::~WaylandEventWatcher() {
  StopProcessingEvents();
}

void WaylandEventWatcher::SetShutdownCb(
    base::OnceCallback<void()> shutdown_cb) {
  DCHECK(shutdown_cb_.is_null());
  shutdown_cb_ = std::move(shutdown_cb);
}

bool WaylandEventWatcher::StartProcessingEvents() {
  DCHECK(display_);
  if (watching_)
    return true;

  DCHECK(display_);
  MaybePrepareReadQueue();
  wl_display_flush(display_);
  return StartWatchingFd(base::MessagePumpForUI::WATCH_READ);
}

bool WaylandEventWatcher::StopProcessingEvents() {
  if (!watching_)
    return false;

  DCHECK(base::CurrentUIThread::IsSet());
  watching_ = false;
  return controller_.StopWatchingFileDescriptor();
}

void WaylandEventWatcher::OnFileCanReadWithoutBlocking(int fd) {
  if (!CheckForErrors()) {
    StopProcessingEvents();
    return;
  }

  if (prepared_) {
    prepared_ = false;
    // Errors will be checked the next time OnFileCanReadWithoutBlocking calls
    // CheckForErrors.
    if (wl_display_read_events(display_) == -1)
      return;
    wl_display_dispatch_pending(display_);
  }

  MaybePrepareReadQueue();

  if (!prepared_)
    return;

  // Automatic Flush.
  int ret = wl_display_flush(display_);
  if (ret != -1 || errno != EAGAIN)
    return;

  // if all data could not be written, errno will be set to EAGAIN and -1
  // returned. In that case, use poll on the display file descriptor to wait for
  // it to become writable again.
  StartWatchingFd(base::MessagePumpForUI::WATCH_WRITE);
}

void WaylandEventWatcher::OnFileCanWriteWithoutBlocking(int fd) {
  int ret = wl_display_flush(display_);
  if (ret != -1 || errno != EAGAIN)
    StartWatchingFd(base::MessagePumpForUI::WATCH_READ);
  else if (ret < 0 && errno != EPIPE && prepared_)
    wl_display_cancel_read(display_);

  // Otherwise just continue watching in the same mode.
}

bool WaylandEventWatcher::StartWatchingFd(
    base::WatchableIOMessagePumpPosix::Mode mode) {
  if (watching_) {
    // Stop watching first.
    watching_ = !controller_.StopWatchingFileDescriptor();
    DCHECK(!watching_);
  }

  DCHECK(base::CurrentUIThread::IsSet());
  int display_fd = wl_display_get_fd(display_);
  watching_ = base::CurrentUIThread::Get()->WatchFileDescriptor(
      display_fd, true, mode, &controller_, this);
  return watching_;
}

void WaylandEventWatcher::MaybePrepareReadQueue() {
  if (prepared_)
    return;

  if (wl_display_prepare_read(display_) != -1) {
    prepared_ = true;
    return;
  }
  // Nothing to read, send events to the queue.
  wl_display_dispatch_pending(display_);
}

bool WaylandEventWatcher::CheckForErrors() {
  // Errors are fatal. If this function returns non-zero the display can no
  // longer be used.
  int err = wl_display_get_error(display_);

  // TODO(crbug.com/1172305): Wayland display_error message should be printed
  // automatically by wl_log(). However, wl_log() does not print anything. Needs
  // investigation.
  if (err) {
    // When |err| is EPROTO, we can still use the |display_| to retrieve the
    // protocol error. Otherwise, get the error string from strerror and
    // shutdown the browser.
    if (err == EPROTO) {
      uint32_t ec, id;
      const struct wl_interface* intf;
      ec = wl_display_get_protocol_error(display_, &intf, &id);
      if (intf) {
        LOG(ERROR) << "Fatal Wayland protocol error " << ec << " on interface "
                   << intf->name << " (object " << id << "). Shutting down..";
      } else {
        LOG(ERROR) << "Fatal Wayland protocol error " << ec
                   << ". Shutting down..";
      }
    } else {
      LOG(ERROR) << "Fatal Wayland communication error: " << std::strerror(err);
    }

    // This can be null in tests.
    if (!shutdown_cb_.is_null())
      std::move(shutdown_cb_).Run();
    return false;
  }

  return true;
}

}  // namespace ui
