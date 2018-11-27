/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_CHANNEL_INTERFACE_H
#define GRPCPP_IMPL_CODEGEN_CHANNEL_INTERFACE_H

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/client_context.h>
#include <grpcpp/impl/codegen/status.h>
#include <grpcpp/impl/codegen/time.h>

namespace grpc {
class ChannelInterface;
class ClientContext;
class CompletionQueue;

template <class R>
class ClientReader;
template <class W>
class ClientWriter;
template <class W, class R>
class ClientReaderWriter;

namespace internal {
class Call;
class CallOpSetInterface;
class RpcMethod;
template <class InputMessage, class OutputMessage>
class BlockingUnaryCallImpl;
template <class InputMessage, class OutputMessage>
class CallbackUnaryCallImpl;
template <class R>
class ClientAsyncReaderFactory;
template <class W>
class ClientAsyncWriterFactory;
template <class W, class R>
class ClientAsyncReaderWriterFactory;
template <class R>
class ClientAsyncResponseReaderFactory;
class InterceptedChannel;
}  // namespace internal

/// Codegen interface for \a grpc::Channel.
class ChannelInterface {
 public:
  virtual ~ChannelInterface() {}
  /// Get the current channel state. If the channel is in IDLE and
  /// \a try_to_connect is set to true, try to connect.
  virtual grpc_connectivity_state GetState(bool try_to_connect) = 0;

  /// Return the \a tag on \a cq when the channel state is changed or \a
  /// deadline expires. \a GetState needs to called to get the current state.
  template <typename T>
  void NotifyOnStateChange(grpc_connectivity_state last_observed, T deadline,
                           CompletionQueue* cq, void* tag) {
    TimePoint<T> deadline_tp(deadline);
    NotifyOnStateChangeImpl(last_observed, deadline_tp.raw_time(), cq, tag);
  }

  /// Blocking wait for channel state change or \a deadline expiration.
  /// \a GetState needs to called to get the current state.
  template <typename T>
  bool WaitForStateChange(grpc_connectivity_state last_observed, T deadline) {
    TimePoint<T> deadline_tp(deadline);
    return WaitForStateChangeImpl(last_observed, deadline_tp.raw_time());
  }

  /// Wait for this channel to be connected
  template <typename T>
  bool WaitForConnected(T deadline) {
    grpc_connectivity_state state;
    while ((state = GetState(true)) != GRPC_CHANNEL_READY) {
      if (!WaitForStateChange(state, deadline)) return false;
    }
    return true;
  }

 private:
  template <class R>
  friend class ::grpc::ClientReader;
  template <class W>
  friend class ::grpc::ClientWriter;
  template <class W, class R>
  friend class ::grpc::ClientReaderWriter;
  template <class R>
  friend class ::grpc::internal::ClientAsyncReaderFactory;
  template <class W>
  friend class ::grpc::internal::ClientAsyncWriterFactory;
  template <class W, class R>
  friend class ::grpc::internal::ClientAsyncReaderWriterFactory;
  template <class R>
  friend class ::grpc::internal::ClientAsyncResponseReaderFactory;
  template <class InputMessage, class OutputMessage>
  friend class ::grpc::internal::BlockingUnaryCallImpl;
  template <class InputMessage, class OutputMessage>
  friend class ::grpc::internal::CallbackUnaryCallImpl;
  friend class ::grpc::internal::RpcMethod;
  friend class ::grpc::internal::InterceptedChannel;
  virtual internal::Call CreateCall(const internal::RpcMethod& method,
                                    ClientContext* context,
                                    CompletionQueue* cq) = 0;
  virtual void PerformOpsOnCall(internal::CallOpSetInterface* ops,
                                internal::Call* call) = 0;
  virtual void* RegisterMethod(const char* method) = 0;
  virtual void NotifyOnStateChangeImpl(grpc_connectivity_state last_observed,
                                       gpr_timespec deadline,
                                       CompletionQueue* cq, void* tag) = 0;
  virtual bool WaitForStateChangeImpl(grpc_connectivity_state last_observed,
                                      gpr_timespec deadline) = 0;

  // EXPERIMENTAL
  // This is needed to keep codegen_test_minimal happy. InterceptedChannel needs
  // to make use of this but can't directly call Channel's implementation
  // because of the test.
  // Returns an empty Call object (rather than being pure) since this is a new
  // method and adding a new pure method to an interface would be a breaking
  // change (even though this is private and non-API)
  virtual internal::Call CreateCallInternal(const internal::RpcMethod& method,
                                            ClientContext* context,
                                            CompletionQueue* cq,
                                            size_t interceptor_pos) {
    return internal::Call();
  }

  // EXPERIMENTAL
  // A method to get the callbackable completion queue associated with this
  // channel. If the return value is nullptr, this channel doesn't support
  // callback operations.
  // TODO(vjpai): Consider a better default like using a global CQ
  // Returns nullptr (rather than being pure) since this is a post-1.0 method
  // and adding a new pure method to an interface would be a breaking change
  // (even though this is private and non-API)
  virtual CompletionQueue* CallbackCQ() { return nullptr; }
};
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CHANNEL_INTERFACE_H
