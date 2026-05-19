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

#ifndef GRPCPP_SERVER_SESSION_H
#define GRPCPP_SERVER_SESSION_H

#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/impl/server_callback_handlers.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/server.h>
#include <grpcpp/support/status.h>

#include <functional>

namespace grpc {

namespace internal {

template <class RequestType>
class CallbackSessionHandler : public CallbackSessionHandlerImpl<RequestType> {
 public:
  explicit CallbackSessionHandler(
      std::function<ServerSessionReactor*(grpc::CallbackServerContext*,
                                          const RequestType*)>
          get_reactor,
      grpc::Service* service = nullptr)
      : CallbackSessionHandlerImpl<RequestType>(std::move(get_reactor),
                                                service) {}
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_SERVER_SESSION_H
