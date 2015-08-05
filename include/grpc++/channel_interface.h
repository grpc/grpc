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
class CallInterface;

class ChannelInterface : public CallHook,
                         public std::enable_shared_from_this<ChannelInterface> {
 public:
  virtual ~ChannelInterface() {}

  virtual void* RegisterMethod(const char* method_name) = 0;
  virtual Call CreateCall(const RpcMethod& method, ClientContext* context,
                          CompletionQueue* cq) = 0;

  virtual grpc_connectivity_state GetState(bool try_to_connect) = 0;

  template <typename T>
  virtual void NotifyOnStateChange(grpc_connectivity_state last_observed,
                                   grpc_connectivity_state* optional_new_state,
                                   const T& deadline,
                                   CompletionQueue* cq, void* tag) = 0;
  template <typename T>
  virtual bool WaitForStateChange(grpc_connectivity_state last_observed,
                                  grpc_connectivity_state* new_state,
                                  const T& deadline) = 0;
};

}  // namespace grpc

#endif  // GRPCXX_CHANNEL_INTERFACE_H
