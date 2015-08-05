/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/cpp/client/channel.h"

#include <memory>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>

#include "src/core/profiling/timers.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/config.h>
#include <grpc++/credentials.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/rpc_method.h>
#include <grpc++/status.h>
#include <grpc++/time.h>

namespace grpc {

Channel::Channel(grpc_channel* channel) : c_channel_(channel) {}

Channel::Channel(const grpc::string& host, grpc_channel* channel)
    : host_(host), c_channel_(channel) {}

Channel::~Channel() { grpc_channel_destroy(c_channel_); }

Call Channel::CreateCall(const RpcMethod& method, ClientContext* context,
                         CompletionQueue* cq) {
  const char* host_str = host_.empty() ? NULL : host_.c_str();
  auto c_call =
      method.channel_tag() && context->authority().empty()
          ? grpc_channel_create_registered_call(c_channel_, cq->cq(),
                                                method.channel_tag(),
                                                context->raw_deadline())
          : grpc_channel_create_call(c_channel_, cq->cq(), method.name(),
                                     context->authority().empty()
                                         ? host_str
                                         : context->authority().c_str(),
                                     context->raw_deadline());
  grpc_census_call_set_context(c_call, context->census_context());
  GRPC_TIMER_MARK(GRPC_PTAG_CPP_CALL_CREATED, c_call);
  context->set_call(c_call, shared_from_this());
  return Call(c_call, this, cq);
}

void Channel::PerformOpsOnCall(CallOpSetInterface* ops, Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = 0;
  grpc_op cops[MAX_OPS];
  GRPC_TIMER_BEGIN(GRPC_PTAG_CPP_PERFORM_OPS, call->call());
  ops->FillOps(cops, &nops);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call->call(), cops, nops, ops));
  GRPC_TIMER_END(GRPC_PTAG_CPP_PERFORM_OPS, call->call());
}

void* Channel::RegisterMethod(const char* method) {
  return grpc_channel_register_call(c_channel_, method,
                                    host_.empty() ? NULL : host_.c_str());
}

grpc_connectivity_state Channel::GetState(bool try_to_connect) {
  return grpc_channel_check_connectivity_state(c_channel_, try_to_connect);
}

namespace {
class TagSaver GRPC_FINAL : public CompletionQueueTag {
 public:
  explicit TagSaver(void* tag) : tag_(tag) {}
  ~TagSaver() GRPC_OVERRIDE {}
  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    *tag = tag_;
    delete this;
    return true;
  }
 private:
  void* tag_;
};

template <typename T>
void NotifyOnStateChangeShared(grpc_channel* channel,
                               grpc_connectivity_state last_observed,
                               const T& deadline,
                               CompletionQueue* cq, void* tag) {
  TimePoint<T> deadline_tp(deadline);
  TagSaver* tag_saver = new TagSaver(tag);
  grpc_channel_watch_connectivity_state(
      channel, last_observed, deadline_tp.raw_time(), cq->cq(), tag_saver);
}

template <typename T>
bool WaitForStateChangeShared(grpc_channel* channel,
                              grpc_connectivity_state last_observed,
                              const T& deadline) {
  CompletionQueue cq;
  bool ok = false;
  void* tag = NULL;
  NotifyOnStateChangeShared(channel, last_observed, deadline, &cq, NULL);
  cq.Next(&tag, &ok);
  GPR_ASSERT(tag == NULL);
  return ok;
}

}  // namespace

void Channel::NotifyOnStateChange(grpc_connectivity_state last_observed,
                                  gpr_timespec deadline,
                                  CompletionQueue* cq, void* tag) {
  NotifyOnStateChangeShared(c_channel_, last_observed, deadline, cq, tag);
}

bool Channel::WaitForStateChange(grpc_connectivity_state last_observed,
                                 gpr_timespec deadline) {
  return WaitForStateChangeShared(c_channel_, last_observed, deadline);
}

#ifndef GRPC_CXX0X_NO_CHRONO
void Channel::NotifyOnStateChange(
      grpc_connectivity_state last_observed,
      const std::chrono::system_clock::time_point& deadline,
      CompletionQueue* cq, void* tag) {
  NotifyOnStateChangeShared(c_channel_, last_observed, deadline, cq, tag);
}

bool Channel::WaitForStateChange(
      grpc_connectivity_state last_observed,
      const std::chrono::system_clock::time_point& deadline) {
  return WaitForStateChangeShared(c_channel_, last_observed, deadline);
}
#endif  // !GRPC_CXX0X_NO_CHRONO
}  // namespace grpc
