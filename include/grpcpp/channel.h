/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCPP_CHANNEL_H
#define GRPCPP_CHANNEL_H

#include <memory>

#include <grpc/grpc.h>
#include <grpcpp/impl/call.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/grpc_library.h>

struct grpc_channel;

namespace grpc {
/// Channels represent a connection to an endpoint. Created by \a CreateChannel.
class Channel final : public ChannelInterface,
                      public internal::CallHook,
                      public std::enable_shared_from_this<Channel>,
                      private GrpcLibraryCodegen {
 public:
  ~Channel();

  /// Get the current channel state. If the channel is in IDLE and
  /// \a try_to_connect is set to true, try to connect.
  grpc_connectivity_state GetState(bool try_to_connect) override;

  /// Returns the LB policy name, or the empty string if not yet available.
  grpc::string GetLoadBalancingPolicyName() const;

  /// Returns the service config in JSON form, or the empty string if
  /// not available.
  grpc::string GetServiceConfigJSON() const;

 private:
  template <class InputMessage, class OutputMessage>
  friend class internal::BlockingUnaryCallImpl;
  friend std::shared_ptr<Channel> CreateChannelInternal(
      const grpc::string& host, grpc_channel* c_channel);
  Channel(const grpc::string& host, grpc_channel* c_channel);

  internal::Call CreateCall(const internal::RpcMethod& method,
                            ClientContext* context,
                            CompletionQueue* cq) override;
  void PerformOpsOnCall(internal::CallOpSetInterface* ops,
                        internal::Call* call) override;
  void* RegisterMethod(const char* method) override;

  void NotifyOnStateChangeImpl(grpc_connectivity_state last_observed,
                               gpr_timespec deadline, CompletionQueue* cq,
                               void* tag) override;
  bool WaitForStateChangeImpl(grpc_connectivity_state last_observed,
                              gpr_timespec deadline) override;

  const grpc::string host_;
  grpc_channel* const c_channel_;  // owned
};

}  // namespace grpc

#endif  // GRPCPP_CHANNEL_H
