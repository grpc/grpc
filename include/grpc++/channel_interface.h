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

#ifndef GRPCXX_CHANNEL_INTERFACE_H
#define GRPCXX_CHANNEL_INTERFACE_H

#include <memory>

#include <grpc/grpc.h>
#include <grpc++/status.h>
#include <grpc++/impl/call.h>

struct grpc_call;

namespace grpc {
class Call;
class CallOpBuffer;
class ClientContext;
class CompletionQueue;
class RpcMethod;

class ChannelInterface : public CallHook,
                         public std::enable_shared_from_this<ChannelInterface> {
 public:
  virtual ~ChannelInterface() {}

  virtual void* RegisterMethod(const char* method_name) = 0;
  virtual Call CreateCall(const RpcMethod& method, ClientContext* context,
                          CompletionQueue* cq) = 0;

  // Get the current channel state. If the channel is in IDLE and try_to_connect
  // is set to true, try to connect.
  virtual grpc_connectivity_state GetState(bool try_to_connect) = 0;

  // Return the tag on cq when the channel state is changed or deadline expires.
  // GetState needs to called to get the current state.
  template <typename T>
  void NotifyOnStateChange(grpc_connectivity_state last_observed, T deadline,
                           CompletionQueue* cq, void* tag) {
    TimePoint<T> deadline_tp(deadline);
    NotifyOnStateChangeImpl(last_observed, deadline_tp.raw_time(), cq, tag);
  }

  // Blocking wait for channel state change or deadline expiration.
  // GetState needs to called to get the current state.
  template <typename T>
  bool WaitForStateChange(grpc_connectivity_state last_observed, T deadline) {
    TimePoint<T> deadline_tp(deadline);
    return WaitForStateChangeImpl(last_observed, deadline_tp.raw_time());
  }

 private:
  virtual void NotifyOnStateChangeImpl(grpc_connectivity_state last_observed,
                                       gpr_timespec deadline,
                                       CompletionQueue* cq, void* tag) = 0;
  virtual bool WaitForStateChangeImpl(grpc_connectivity_state last_observed,
                                      gpr_timespec deadline) = 0;
};

}  // namespace grpc

#endif  // GRPCXX_CHANNEL_INTERFACE_H
