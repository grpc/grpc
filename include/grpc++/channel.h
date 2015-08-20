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

#ifndef GRPCXX_CHANNEL_H
#define GRPCXX_CHANNEL_H

#include <memory>

#include <grpc/grpc.h>
#include <grpc++/config.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/grpc_library.h>

struct grpc_channel;

namespace grpc {
class CallOpSetInterface;
class ChannelArguments;
class CompletionQueue;
class Credentials;
class SecureCredentials;

template <class R>
class ClientReader;
template <class W>
class ClientWriter;
template <class R, class W>
class ClientReaderWriter;
template <class R>
class ClientAsyncReader;
template <class W>
class ClientAsyncWriter;
template <class R, class W>
class ClientAsyncReaderWriter;
template <class R>
class ClientAsyncResponseReader;

class Channel GRPC_FINAL : public GrpcLibrary,
                           public CallHook,
                           public std::enable_shared_from_this<Channel> {
 public:
  explicit Channel(grpc_channel* c_channel);
  Channel(const grpc::string& host, grpc_channel* c_channel);
  ~Channel();

  // Get the current channel state. If the channel is in IDLE and try_to_connect
  // is set to true, try to connect.
  grpc_connectivity_state GetState(bool try_to_connect);

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
  template <class R>
  friend class ::grpc::ClientReader;
  template <class W>
  friend class ::grpc::ClientWriter;
  template <class R, class W>
  friend class ::grpc::ClientReaderWriter;
  template <class R>
  friend class ::grpc::ClientAsyncReader;
  template <class W>
  friend class ::grpc::ClientAsyncWriter;
  template <class R, class W>
  friend class ::grpc::ClientAsyncReaderWriter;
  template <class R>
  friend class ::grpc::ClientAsyncResponseReader;
  template <class InputMessage, class OutputMessage>
  friend Status BlockingUnaryCall(Channel* channel, const RpcMethod& method,
                                  ClientContext* context,
                                  const InputMessage& request,
                                  OutputMessage* result);
  friend class ::grpc::RpcMethod;

  Call CreateCall(const RpcMethod& method, ClientContext* context,
                  CompletionQueue* cq);
  void PerformOpsOnCall(CallOpSetInterface* ops, Call* call);
  void* RegisterMethod(const char* method);

  void NotifyOnStateChangeImpl(grpc_connectivity_state last_observed,
                               gpr_timespec deadline, CompletionQueue* cq,
                               void* tag);
  bool WaitForStateChangeImpl(grpc_connectivity_state last_observed,
                              gpr_timespec deadline);

  const grpc::string host_;
  grpc_channel* const c_channel_;  // owned
};

}  // namespace grpc

#endif  // GRPCXX_CHANNEL_H
