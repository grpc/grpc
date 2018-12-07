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

#include <atomic>
#include <vector>

#include <grpcpp/impl/codegen/interceptor.h>
#include <grpcpp/impl/codegen/rpc_method.h>
#include <grpcpp/impl/codegen/string_ref.h>

namespace grpc {

class ServerContext;

namespace internal {
class InterceptorBatchMethodsImpl;
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
  enum class Type { UNARY, CLIENT_STREAMING, SERVER_STREAMING, BIDI_STREAMING };

  ~ServerRpcInfo(){};

  ServerRpcInfo(const ServerRpcInfo&) = delete;
  ServerRpcInfo(ServerRpcInfo&&) = default;
  ServerRpcInfo& operator=(ServerRpcInfo&&) = default;

  // Getter methods
  const char* method() const { return method_; }
  Type type() const { return type_; }
  grpc::ServerContext* server_context() { return ctx_; }

 private:
  static_assert(Type::UNARY ==
                    static_cast<Type>(internal::RpcMethod::NORMAL_RPC),
                "violated expectation about Type enum");
  static_assert(Type::CLIENT_STREAMING ==
                    static_cast<Type>(internal::RpcMethod::CLIENT_STREAMING),
                "violated expectation about Type enum");
  static_assert(Type::SERVER_STREAMING ==
                    static_cast<Type>(internal::RpcMethod::SERVER_STREAMING),
                "violated expectation about Type enum");
  static_assert(Type::BIDI_STREAMING ==
                    static_cast<Type>(internal::RpcMethod::BIDI_STREAMING),
                "violated expectation about Type enum");

  ServerRpcInfo(grpc::ServerContext* ctx, const char* method,
                internal::RpcMethod::RpcType type)
      : ctx_(ctx), method_(method), type_(static_cast<Type>(type)) {
    ref_.store(1);
  }

  // Runs interceptor at pos \a pos.
  void RunInterceptor(
      experimental::InterceptorBatchMethods* interceptor_methods, size_t pos) {
    GPR_CODEGEN_ASSERT(pos < interceptors_.size());
    interceptors_[pos]->Intercept(interceptor_methods);
  }

  void RegisterInterceptors(
      const std::vector<
          std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>&
          creators) {
    for (const auto& creator : creators) {
      interceptors_.push_back(std::unique_ptr<experimental::Interceptor>(
          creator->CreateServerInterceptor(this)));
    }
  }

  void Ref() { ref_++; }
  void Unref() {
    if (--ref_ == 0) {
      delete this;
    }
  }

  grpc::ServerContext* ctx_ = nullptr;
  const char* method_ = nullptr;
  const Type type_;
  std::atomic_int ref_;
  std::vector<std::unique_ptr<experimental::Interceptor>> interceptors_;

  friend class internal::InterceptorBatchMethodsImpl;
  friend class grpc::ServerContext;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_INTERCEPTOR_H
