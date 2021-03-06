// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/lib/thread_safe_forwarder_base.h"

#include <utility>

#include "base/check.h"
#include "base/stl_util.h"
#include "mojo/public/cpp/bindings/sync_call_restrictions.h"
#include "mojo/public/cpp/bindings/sync_event_watcher.h"

namespace mojo {

namespace internal {

ThreadSafeForwarderBase::ThreadSafeForwarderBase(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    ForwardMessageCallback forward,
    ForwardMessageWithResponderCallback forward_with_responder,
    ForceAsyncSendCallback force_async_send,
    const AssociatedGroup& associated_group)
    : task_runner_(std::move(task_runner)),
      forward_(std::move(forward)),
      forward_with_responder_(std::move(forward_with_responder)),
      force_async_send_(std::move(force_async_send)),
      associated_group_(associated_group),
      sync_calls_(new InProgressSyncCalls()) {}

ThreadSafeForwarderBase::~ThreadSafeForwarderBase() {
  // If there are ongoing sync calls signal their completion now.
  base::AutoLock l(sync_calls_->lock);
  for (auto* pending_response : sync_calls_->pending_responses)
    pending_response->event.Signal();
}

bool ThreadSafeForwarderBase::PrefersSerializedMessages() {
  // NOTE: This means SharedRemote etc will ignore lazy serialization hints and
  // will always eagerly serialize messages.
  return true;
}

bool ThreadSafeForwarderBase::Accept(Message* message) {
  message->SerializeHandles(associated_group_.GetController());
  task_runner_->PostTask(FROM_HERE,
                         base::BindOnce(forward_, std::move(*message)));
  return true;
}

bool ThreadSafeForwarderBase::AcceptWithResponder(
    Message* message,
    std::unique_ptr<MessageReceiver> responder) {
  message->SerializeHandles(associated_group_.GetController());

  // Async messages are always posted (even if |task_runner_| runs tasks on
  // this sequence) to guarantee that two async calls can't be reordered.
  if (!message->has_flag(Message::kFlagIsSync)) {
    auto reply_forwarder =
        std::make_unique<ForwardToCallingThread>(std::move(responder));
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(forward_with_responder_, std::move(*message),
                                  std::move(reply_forwarder)));
    return true;
  }

  SyncCallRestrictions::AssertSyncCallAllowed();

  // If the Remote is bound to this sequence, send the message immediately and
  // let Remote use its own internal sync waiting mechanism.
  if (task_runner_->RunsTasksInCurrentSequence()) {
    const base::WeakPtr<ThreadSafeForwarderBase> weak_self =
        weak_ptr_factory_.GetWeakPtr();
    ++sync_call_nesting_level_;
    if (sync_call_nesting_level_ == 1)
      force_async_send_.Run(false);
    forward_with_responder_.Run(std::move(*message), std::move(responder));
    if (weak_self) {
      // NOTE: |this| may be deleted within the callback run above.
      --sync_call_nesting_level_;
      if (!sync_call_nesting_level_)
        force_async_send_.Run(true);
    }
    return true;
  }

  // If the Remote is bound on another sequence, post the call.
  auto response = base::MakeRefCounted<SyncResponseInfo>();
  auto response_signaler = std::make_unique<SyncResponseSignaler>(response);
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(forward_with_responder_, std::move(*message),
                                std::move(response_signaler)));

  // Save the pending SyncResponseInfo so that if the sync call deletes
  // |this|, we can signal the completion of the call to return from
  // SyncWatch().
  auto sync_calls = sync_calls_;
  {
    base::AutoLock l(sync_calls->lock);
    sync_calls->pending_responses.push_back(response.get());
  }

  auto assign_true = [](bool* b) { *b = true; };
  bool event_signaled = false;
  SyncEventWatcher watcher(&response->event,
                           base::BindRepeating(assign_true, &event_signaled));
  const bool* stop_flags[] = {&event_signaled};
  watcher.SyncWatch(stop_flags, 1);

  {
    base::AutoLock l(sync_calls->lock);
    base::Erase(sync_calls->pending_responses, response.get());
  }

  if (response->received)
    ignore_result(responder->Accept(&response->message));

  return true;
}

ThreadSafeForwarderBase::SyncResponseInfo::SyncResponseInfo() = default;

ThreadSafeForwarderBase::SyncResponseInfo::~SyncResponseInfo() = default;

ThreadSafeForwarderBase::SyncResponseSignaler::SyncResponseSignaler(
    scoped_refptr<SyncResponseInfo> response)
    : response_(response) {}

ThreadSafeForwarderBase::SyncResponseSignaler::~SyncResponseSignaler() {
  // If Accept() was not called we must still notify the waiter that the
  // sync call is finished.
  if (response_)
    response_->event.Signal();
}

bool ThreadSafeForwarderBase::SyncResponseSignaler::Accept(Message* message) {
  response_->message = std::move(*message);
  response_->received = true;
  response_->event.Signal();
  response_ = nullptr;
  return true;
}

ThreadSafeForwarderBase::InProgressSyncCalls::InProgressSyncCalls() = default;

ThreadSafeForwarderBase::InProgressSyncCalls::~InProgressSyncCalls() = default;

ThreadSafeForwarderBase::ForwardToCallingThread::ForwardToCallingThread(
    std::unique_ptr<MessageReceiver> responder)
    : responder_(std::move(responder)),
      caller_task_runner_(base::SequencedTaskRunnerHandle::Get()) {}

ThreadSafeForwarderBase::ForwardToCallingThread::~ForwardToCallingThread() {
  caller_task_runner_->DeleteSoon(FROM_HERE, std::move(responder_));
}

bool ThreadSafeForwarderBase::ForwardToCallingThread::Accept(Message* message) {
  // The current instance will be deleted when this method returns, so we
  // have to relinquish the responder's ownership so it does not get
  // deleted.
  caller_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ForwardToCallingThread::CallAcceptAndDeleteResponder,
                     std::move(responder_), std::move(*message)));
  return true;
}

// static
void ThreadSafeForwarderBase::ForwardToCallingThread::
    CallAcceptAndDeleteResponder(std::unique_ptr<MessageReceiver> responder,
                                 Message message) {
  ignore_result(responder->Accept(&message));
}

}  // namespace internal

}  // namespace mojo
