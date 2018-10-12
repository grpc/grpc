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

#include <vector>

#include <grpc/impl/codegen/log.h>
#include <grpcpp/impl/codegen/interceptor.h>
#include <grpcpp/impl/codegen/string_ref.h>

namespace grpc {

class ClientContext;
class Channel;

namespace internal {
template <int I>
class CallNoOp;
}

namespace experimental {
class ClientRpcInfo;

class ClientInterceptor {
 public:
  virtual ~ClientInterceptor() {}

  virtual void Intercept(InterceptorBatchMethods* methods) = 0;
};

class ClientInterceptorFactoryInterface {
 public:
  virtual ~ClientInterceptorFactoryInterface() {}
  virtual ClientInterceptor* CreateClientInterceptor(ClientRpcInfo* info) = 0;
};

class ClientRpcInfo {
 public:
  ClientRpcInfo() {}
  ClientRpcInfo(grpc::ClientContext* ctx, const char* method,
                const grpc::Channel* channel,
                const std::vector<std::unique_ptr<
                    experimental::ClientInterceptorFactoryInterface>>& creators)
      : ctx_(ctx), method_(method), channel_(channel) {
    for (const auto& creator : creators) {
      interceptors_.push_back(std::unique_ptr<experimental::ClientInterceptor>(
          creator->CreateClientInterceptor(this)));
    }
  }
  ~ClientRpcInfo(){};

  ClientRpcInfo(const ClientRpcInfo&) = delete;
  ClientRpcInfo(ClientRpcInfo&&) = default;
  ClientRpcInfo& operator=(ClientRpcInfo&&) = default;

  // Getter methods
  const char* method() { return method_; }
  const Channel* channel() { return channel_; }
  // const grpc::InterceptedMessage& outgoing_message();
  // grpc::InterceptedMessage *mutable_outgoing_message();
  // const grpc::InterceptedMessage& received_message();
  // grpc::InterceptedMessage *mutable_received_message();

  // const std::multimap<grpc::string, grpc::string>* client_initial_metadata()
  // { return &ctx_->send_initial_metadata_; }  const
  // std::multimap<grpc::string_ref, grpc::string_ref>*
  // server_initial_metadata() { return &ctx_->GetServerInitialMetadata(); }
  // const std::multimap<grpc::string_ref, grpc::string_ref>*
  // server_trailing_metadata() { return &ctx_->GetServerTrailingMetadata(); }
  // const Status *status();

  // template <class M>
  //    void set_outgoing_message(M* msg); // edit outgoing message
  // template <class M>
  //    void set_received_message(M* msg); // edit received message
  // for hijacking (can be called multiple times for streaming)
  // template <class M>
  //    void inject_received_message(M* msg);
  // void set_client_initial_metadata(
  //    const std::multimap<grpc::string, grpc::string>& overwrite);
  // void set_server_initial_metadata(const std::multimap<grpc::string,
  // grpc::string>& overwrite);  void set_server_trailing_metadata(const
  // std::multimap<grpc::string, grpc::string>& overwrite);  void
  // set_status(Status status);
 public:
  /* Runs interceptor at pos \a pos. If \a reverse is set, the interceptor order
   * is the reverse */
  void RunInterceptor(
      experimental::InterceptorBatchMethods* interceptor_methods,
      unsigned int pos) {
    GPR_CODEGEN_ASSERT(pos < interceptors_.size());
    interceptors_[pos]->Intercept(interceptor_methods);
  }

  grpc::ClientContext* ctx_ = nullptr;
  const char* method_ = nullptr;
  const grpc::Channel* channel_ = nullptr;

 public:
  std::vector<std::unique_ptr<experimental::ClientInterceptor>> interceptors_;
  bool hijacked_ = false;
  int hijacked_interceptor_ = false;
  // template <class Op1 = internal::CallNoOp<1>, class Op2 =
  // internal::CallNoOp<2>,
  //         class Op3 = internal::CallNoOp<3>, class Op4 =
  //         internal::CallNoOp<4>, class Op5 = internal::CallNoOp<5>, class Op6
  //         = internal::CallNoOp<6>>
  // friend class internal::InterceptorBatchMethodsImpl;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_INTERCEPTOR_H
