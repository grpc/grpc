/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_CLIENT_INTERCEPTOR_H
#define GRPCPP_IMPL_CODEGEN_CLIENT_INTERCEPTOR_H

#include <grpcpp/impl/codegen/interceptor.h>

namespace grpc {
namespace experimental {
class ClientInterceptor {
 public:
  virtual ~ClientInterceptor() {}

  virtual void Intercept(InterceptorBatchMethods* methods) = 0;
};

class ClientRpcInfo {};

class ClientInterceptorFactoryInterface {
 public:
  virtual ~ClientInterceptorFactoryInterface() {}
  virtual ClientInterceptor* CreateClientInterceptor(ClientRpcInfo* info) = 0;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_INTERCEPTOR_H
