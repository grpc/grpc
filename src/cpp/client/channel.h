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

#ifndef GRPC_INTERNAL_CPP_CLIENT_CHANNEL_H
#define GRPC_INTERNAL_CPP_CLIENT_CHANNEL_H

#include <memory>

#include <grpc++/channel_interface.h>
#include <grpc++/config.h>
#include <grpc++/impl/grpc_library.h>

struct grpc_channel;

namespace grpc {
class Call;
class CallOpSetInterface;
class ChannelArguments;
class CompletionQueue;
class Credentials;
class StreamContextInterface;

class Channel GRPC_FINAL : public GrpcLibrary, public ChannelInterface {
 public:
  explicit Channel(grpc_channel* c_channel);
  Channel(const grpc::string& host, grpc_channel* c_channel);
  ~Channel() GRPC_OVERRIDE;

  void* RegisterMethod(const char* method) GRPC_OVERRIDE;
  Call CreateCall(const RpcMethod& method, ClientContext* context,
                          CompletionQueue* cq) GRPC_OVERRIDE;
  void PerformOpsOnCall(CallOpSetInterface* ops,
                                Call* call) GRPC_OVERRIDE;

  grpc_connectivity_state GetState(bool try_to_connect) GRPC_OVERRIDE;

  void NotifyOnStateChange(grpc_connectivity_state last_observed,
                           gpr_timespec deadline,
                           CompletionQueue* cq, void* tag) GRPC_OVERRIDE;

  bool WaitForStateChange(grpc_connectivity_state last_observed,
                          gpr_timespec deadline) GRPC_OVERRIDE;

#ifndef GRPC_CXX0X_NO_CHRONO
  void NotifyOnStateChange(
      grpc_connectivity_state last_observed,
      const std::chrono::system_clock::time_point& deadline,
      CompletionQueue* cq, void* tag) GRPC_OVERRIDE;
  bool WaitForStateChange(
      grpc_connectivity_state last_observed,
      const std::chrono::system_clock::time_point& deadline) GRPC_OVERRIDE;
#endif  // !GRPC_CXX0X_NO_CHRONO
 private:
  const grpc::string host_;
  grpc_channel* const c_channel_;  // owned
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_CLIENT_CHANNEL_H
