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

#include <grpcpp/impl/codegen/client_context.h>
#include <grpcpp/impl/codegen/interceptor.h>
#include <grpcpp/impl/codegen/string_ref.h>

namespace grpc {
namespace experimental {
class ClientInterceptor {
 public:
  virtual ~ClientInterceptor() {}

  virtual void Intercept(InterceptorBatchMethods* methods) = 0;
};

class ClientRpcInfo {
 public:
  ClientRpcInfo(grpc::ClientContext* ctx, const char* method,
                const grpc::Channel* channel)
      : ctx_(ctx), method_(method), channel_(channel) {}
  ~ClientRpcInfo(){};

  // Getter methods
  const char* method() { return method_; }
  string peer() { return ctx_->peer(); }
  const Channel* channel() { return channel_; }
  // const grpc::InterceptedMessage& outgoing_message();
  // grpc::InterceptedMessage *mutable_outgoing_message();
  // const grpc::InterceptedMessage& received_message();
  // grpc::InterceptedMessage *mutable_received_message();
  std::shared_ptr<const AuthContext> auth_context() {
    return ctx_->auth_context();
  }
  const struct census_context* census_context() {
    return ctx_->census_context();
  }
  gpr_timespec deadline() { return ctx_->raw_deadline(); }
  // const std::multimap<grpc::string, grpc::string>* client_initial_metadata()
  // { return &ctx_->send_initial_metadata_; }  const
  // std::multimap<grpc::string_ref, grpc::string_ref>*
  // server_initial_metadata() { return &ctx_->GetServerInitialMetadata(); }
  // const std::multimap<grpc::string_ref, grpc::string_ref>*
  // server_trailing_metadata() { return &ctx_->GetServerTrailingMetadata(); }
  // const Status *status();

  // Setter methods
  template <typename T>
  void set_deadline(const T& deadline) {
    ctx_->set_deadline(deadline);
  }
  void set_census_context(struct census_context* cc) {
    ctx_->set_census_context(cc);
  }
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
 private:
  grpc::ClientContext* ctx_;
  const char* method_;
  const grpc::Channel* channel_;
};

class ClientInterceptorFactoryInterface {
 public:
  virtual ~ClientInterceptorFactoryInterface() {}
  virtual ClientInterceptor* CreateClientInterceptor(ClientRpcInfo* info) = 0;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_INTERCEPTOR_H
