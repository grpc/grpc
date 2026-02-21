//
//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#ifndef GRPCPP_IMPL_GENERIC_STUB_SESSION_H
#define GRPCPP_IMPL_GENERIC_STUB_SESSION_H

#include <grpcpp/client_context.h>
#include <grpcpp/impl/channel_interface.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/stub_options.h>

#include <functional>
#include <memory>
#include <string>

namespace grpc {
namespace internal {

// Experimental API for creating a ClientCallbackSession. This API is
// experimental (and visibility restricted), and may be removed or changed
// without notice.
template <class RequestType, class ResponseType>
class GenericStubSession {
 public:
  explicit GenericStubSession(std::shared_ptr<grpc::ChannelInterface> channel)
      : channel_(std::move(channel)) {}

  /// Setup a session call to a named method \a method using
  /// \a context and tied to \a reactor . Like any other reactor-based RPC,
  /// it will not be activated until StartCall is invoked on its reactor.
  void PrepareSessionCall(ClientContext* context, const std::string& method,
                          StubOptions options, const RequestType* request,
                          ClientSessionReactor* reactor,
                          std::function<void(grpc::Status)> on_completion) {
    internal::ClientCallbackSessionFactory::Create<RequestType>(
        channel_.get(),
        grpc::internal::RpcMethod(method.c_str(), options.suffix_for_stats(),
                                  grpc::internal::RpcMethod::SESSION_RPC),
        context, request, reactor, std::move(on_completion));
  }

 private:
  std::shared_ptr<grpc::ChannelInterface> channel_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_GENERIC_STUB_SESSION_H
