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

#ifndef GRPCPP_IMPL_CODEGEN_SERVER_INTERCEPTOR_H
#define GRPCPP_IMPL_CODEGEN_SERVER_INTERCEPTOR_H

#include <vector>

#include <grpc/impl/codegen/log.h>
#include <grpcpp/impl/codegen/interceptor.h>
#include <grpcpp/impl/codegen/string_ref.h>

namespace grpc {

class ServerContext;

namespace internal {
template <int I>
class CallNoOp;
}

namespace experimental {
class ServerRpcInfo;

class ServerInterceptorFactoryInterface {
 public:
  virtual ~ServerInterceptorFactoryInterface() {}
  virtual Interceptor* CreateServerInterceptor(ServerRpcInfo* info) = 0;
};

class ServerRpcInfo {
 public:
  ServerRpcInfo() {}
  ServerRpcInfo(grpc::ServerContext* ctx, const char* method,
                const std::vector<std::unique_ptr<
                    experimental::ServerInterceptorFactoryInterface>>& creators)
      : ctx_(ctx), method_(method) {
    for (const auto& creator : creators) {
      interceptors_.push_back(std::unique_ptr<experimental::Interceptor>(
          creator->CreateServerInterceptor(this)));
    }
  }
  ~ServerRpcInfo(){};

  ServerRpcInfo(const ServerRpcInfo&) = delete;
  ServerRpcInfo(ServerRpcInfo&&) = default;
  ServerRpcInfo& operator=(ServerRpcInfo&&) = default;

  // Getter methods
  const char* method() { return method_; }
  grpc::ServerContext* server_context() { return ctx_; }

 public:
  /* Runs interceptor at pos \a pos. If \a reverse is set, the interceptor order
   * is the reverse */
  void RunInterceptor(
      experimental::InterceptorBatchMethods* interceptor_methods,
      unsigned int pos) {
    GPR_CODEGEN_ASSERT(pos < interceptors_.size());
    interceptors_[pos]->Intercept(interceptor_methods);
  }

 private:
  grpc::ServerContext* ctx_ = nullptr;
  const char* method_ = nullptr;

 public:
  std::vector<std::unique_ptr<experimental::Interceptor>> interceptors_;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_INTERCEPTOR_H
