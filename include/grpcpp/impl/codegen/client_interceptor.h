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

#include <memory>
#include <vector>

#include <grpcpp/impl/codegen/interceptor.h>
#include <grpcpp/impl/codegen/string_ref.h>

namespace grpc {

class ClientContext;
class Channel;

namespace internal {
class InterceptorBatchMethodsImpl;
}

namespace experimental {
class ClientRpcInfo;

class ClientInterceptorFactoryInterface {
 public:
  virtual ~ClientInterceptorFactoryInterface() {}
  virtual Interceptor* CreateClientInterceptor(ClientRpcInfo* info) = 0;
};
}  // namespace experimental

namespace internal {
extern experimental::ClientInterceptorFactoryInterface*
    g_global_client_interceptor_factory;
}

namespace experimental {
class ClientRpcInfo {
 public:
  ClientRpcInfo() {}

  ~ClientRpcInfo(){};

  ClientRpcInfo(const ClientRpcInfo&) = delete;
  ClientRpcInfo(ClientRpcInfo&&) = default;
  ClientRpcInfo& operator=(ClientRpcInfo&&) = default;

  // Getter methods
  const char* method() { return method_; }
  ChannelInterface* channel() { return channel_; }
  grpc::ClientContext* client_context() { return ctx_; }

 private:
  ClientRpcInfo(grpc::ClientContext* ctx, const char* method,
                grpc::ChannelInterface* channel)
      : ctx_(ctx), method_(method), channel_(channel) {}
  // Runs interceptor at pos \a pos.
  void RunInterceptor(
      experimental::InterceptorBatchMethods* interceptor_methods, size_t pos) {
    GPR_CODEGEN_ASSERT(pos < interceptors_.size());
    interceptors_[pos]->Intercept(interceptor_methods);
  }

  void RegisterInterceptors(
      const std::vector<std::unique_ptr<
          experimental::ClientInterceptorFactoryInterface>>& creators,
      size_t interceptor_pos) {
    if (interceptor_pos > creators.size()) {
      // No interceptors to register
      return;
    }
    for (auto it = creators.begin() + interceptor_pos; it != creators.end();
         ++it) {
      interceptors_.push_back(std::unique_ptr<experimental::Interceptor>(
          (*it)->CreateClientInterceptor(this)));
    }
    if (internal::g_global_client_interceptor_factory != nullptr) {
      interceptors_.push_back(std::unique_ptr<experimental::Interceptor>(
          internal::g_global_client_interceptor_factory
              ->CreateClientInterceptor(this)));
    }
  }

  grpc::ClientContext* ctx_ = nullptr;
  const char* method_ = nullptr;
  grpc::ChannelInterface* channel_ = nullptr;
  std::vector<std::unique_ptr<experimental::Interceptor>> interceptors_;
  bool hijacked_ = false;
  size_t hijacked_interceptor_ = 0;

  friend class internal::InterceptorBatchMethodsImpl;
  friend class grpc::ClientContext;
};

// PLEASE DO NOT USE THIS. ALWAYS PREFER PER CHANNEL INTERCEPTORS OVER A GLOBAL
// INTERCEPTOR. IF USAGE IS ABSOLUTELY NECESSARY, PLEASE READ THE SAFETY NOTES.
// Registers a global client interceptor factory object, which is used for all
// RPCs made in this process.  If the argument is nullptr, the global
// interceptor factory is deregistered. The application is responsible for
// maintaining the life of the object while gRPC operations are in progress. It
// is unsafe to try to register/deregister if any gRPC operation is in progress.
// For safety, it is in the best interests of the developer to register the
// global interceptor factory once at the start of the process before any gRPC
// operations have begun. Deregistration is optional since gRPC does not
// maintain any references to the object.
void RegisterGlobalClientInterceptorFactory(
    ClientInterceptorFactoryInterface* factory);

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_INTERCEPTOR_H
