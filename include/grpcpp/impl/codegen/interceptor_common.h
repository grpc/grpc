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

#ifndef GRPCPP_IMPL_CODEGEN_INTERCEPTOR_COMMON_H
#define GRPCPP_IMPL_CODEGEN_INTERCEPTOR_COMMON_H

#include <grpcpp/impl/codegen/client_interceptor.h>
#include <grpcpp/impl/codegen/server_interceptor.h>

#include <grpc/impl/codegen/grpc_types.h>

namespace grpc {
namespace internal {

/// Internal methods for setting the state
class InternalInterceptorBatchMethods
    : public experimental::InterceptorBatchMethods {
 public:
  virtual ~InternalInterceptorBatchMethods() {}

  virtual void AddInterceptionHookPoint(
      experimental::InterceptionHookPoints type) = 0;

  virtual void SetSendMessage(ByteBuffer* buf) = 0;

  virtual void SetSendInitialMetadata(
      std::multimap<grpc::string, grpc::string>* metadata) = 0;

  virtual void SetSendStatus(grpc_status_code* code,
                             grpc::string* error_details,
                             grpc::string* error_message) = 0;

  virtual void SetSendTrailingMetadata(
      std::multimap<grpc::string, grpc::string>* metadata) = 0;

  virtual void SetRecvMessage(void* message) = 0;

  virtual void SetRecvInitialMetadata(internal::MetadataMap* map) = 0;

  virtual void SetRecvStatus(Status* status) = 0;

  virtual void SetRecvTrailingMetadata(internal::MetadataMap* map) = 0;
};

class InterceptorBatchMethodsImpl : public InternalInterceptorBatchMethods {
 public:
  InterceptorBatchMethodsImpl() {
    for (auto i = 0;
         i < static_cast<int>(
                 experimental::InterceptionHookPoints::NUM_INTERCEPTION_HOOKS);
         i++) {
      hooks_[i] = false;
    }
  }

  virtual ~InterceptorBatchMethodsImpl() {}

  virtual bool QueryInterceptionHookPoint(
      experimental::InterceptionHookPoints type) override {
    return hooks_[static_cast<int>(type)];
  }

  virtual void Proceed() override { /* fill this */
    if (call_->client_rpc_info() != nullptr) {
      return ProceedClient();
    }
    GPR_CODEGEN_ASSERT(call_->server_rpc_info() != nullptr);
    ProceedServer();
  }

  virtual void Hijack() override {
    // Only the client can hijack when sending down initial metadata
    GPR_CODEGEN_ASSERT(!reverse_ && ops_ != nullptr &&
                       call_->client_rpc_info() != nullptr);
    // It is illegal to call Hijack twice
    GPR_CODEGEN_ASSERT(!ran_hijacking_interceptor_);
    auto* rpc_info = call_->client_rpc_info();
    rpc_info->hijacked_ = true;
    rpc_info->hijacked_interceptor_ = curr_iteration_;
    ClearHookPoints();
    ops_->SetHijackingState();
    ran_hijacking_interceptor_ = true;
    rpc_info->RunInterceptor(this, curr_iteration_);
  }

  virtual void AddInterceptionHookPoint(
      experimental::InterceptionHookPoints type) override {
    hooks_[static_cast<int>(type)] = true;
  }

  virtual ByteBuffer* GetSendMessage() override { return send_message_; }

  virtual std::multimap<grpc::string, grpc::string>* GetSendInitialMetadata()
      override {
    return send_initial_metadata_;
  }

  virtual Status GetSendStatus() override {
    return Status(static_cast<StatusCode>(*code_), *error_message_,
                  *error_details_);
  }

  virtual void ModifySendStatus(const Status& status) override {
    *code_ = static_cast<grpc_status_code>(status.error_code());
    *error_details_ = status.error_details();
    *error_message_ = status.error_message();
  }

  virtual std::multimap<grpc::string, grpc::string>* GetSendTrailingMetadata()
      override {
    return send_trailing_metadata_;
  }

  virtual void* GetRecvMessage() override { return recv_message_; }

  virtual std::multimap<grpc::string_ref, grpc::string_ref>*
  GetRecvInitialMetadata() override {
    return recv_initial_metadata_->map();
  }

  virtual Status* GetRecvStatus() override { return recv_status_; }

  virtual std::multimap<grpc::string_ref, grpc::string_ref>*
  GetRecvTrailingMetadata() override {
    return recv_trailing_metadata_->map();
  }

  virtual void SetSendMessage(ByteBuffer* buf) override { send_message_ = buf; }

  virtual void SetSendInitialMetadata(
      std::multimap<grpc::string, grpc::string>* metadata) override {
    send_initial_metadata_ = metadata;
  }

  virtual void SetSendStatus(grpc_status_code* code,
                             grpc::string* error_details,
                             grpc::string* error_message) override {
    code_ = code;
    error_details_ = error_details;
    error_message_ = error_message;
  }

  virtual void SetSendTrailingMetadata(
      std::multimap<grpc::string, grpc::string>* metadata) override {
    send_trailing_metadata_ = metadata;
  }

  virtual void SetRecvMessage(void* message) override {
    recv_message_ = message;
  }

  virtual void SetRecvInitialMetadata(internal::MetadataMap* map) override {
    recv_initial_metadata_ = map;
  }

  virtual void SetRecvStatus(Status* status) override { recv_status_ = status; }

  virtual void SetRecvTrailingMetadata(internal::MetadataMap* map) override {
    recv_trailing_metadata_ = map;
  }

  virtual std::unique_ptr<ChannelInterface> GetInterceptedChannel() override {
    auto* info = call_->client_rpc_info();
    if (info == nullptr) {
      return std::unique_ptr<ChannelInterface>(nullptr);
    }
    // The intercepted channel starts from the interceptor just after the
    // current interceptor
    return std::unique_ptr<ChannelInterface>(new internal::InterceptedChannel(
        reinterpret_cast<grpc::ChannelInterface*>(info->channel()),
        curr_iteration_ + 1));
  }

  // Clears all state
  void ClearState() {
    reverse_ = false;
    ran_hijacking_interceptor_ = false;
    ClearHookPoints();
  }

  // Prepares for Post_recv operations
  void SetReverse() {
    reverse_ = true;
    ran_hijacking_interceptor_ = false;
    ClearHookPoints();
  }

  // This needs to be set before interceptors are run
  void SetCall(Call* call) { call_ = call; }

  // This needs to be set before interceptors are run using RunInterceptors().
  // Alternatively, RunInterceptors(std::function<void(void)> f) can be used.
  void SetCallOpSetInterface(CallOpSetInterface* ops) { ops_ = ops; }

  // Returns true if no interceptors are run. This should be used only by
  // subclasses of CallOpSetInterface. SetCall and SetCallOpSetInterface should
  // have been called before this. After all the interceptors are done running,
  // either ContinueFillOpsAfterInterception or
  // ContinueFinalizeOpsAfterInterception will be called. Note that neither of
  // them is invoked if there were no interceptors registered.
  bool RunInterceptors() {
    GPR_CODEGEN_ASSERT(ops_);
    auto* client_rpc_info = call_->client_rpc_info();
    if (client_rpc_info != nullptr) {
      if (client_rpc_info->interceptors_.size() == 0) {
        return true;
      } else {
        RunClientInterceptors();
        return false;
      }
    }

    auto* server_rpc_info = call_->server_rpc_info();
    if (server_rpc_info == nullptr ||
        server_rpc_info->interceptors_.size() == 0) {
      return true;
    }
    RunServerInterceptors();
    return false;
  }

  // Returns true if no interceptors are run. Returns false otherwise if there
  // are interceptors registered. After the interceptors are done running \a f
  // will be invoked. This is to be used only by BaseAsyncRequest and
  // SyncRequest.
  bool RunInterceptors(std::function<void(void)> f) {
    // This is used only by the server for initial call request
    GPR_CODEGEN_ASSERT(reverse_ == true);
    GPR_CODEGEN_ASSERT(call_->client_rpc_info() == nullptr);
    auto* server_rpc_info = call_->server_rpc_info();
    if (server_rpc_info == nullptr ||
        server_rpc_info->interceptors_.size() == 0) {
      return true;
    }
    callback_ = std::move(f);
    RunServerInterceptors();
    return false;
  }

 private:
  void RunClientInterceptors() {
    auto* rpc_info = call_->client_rpc_info();
    if (!reverse_) {
      curr_iteration_ = 0;
    } else {
      if (rpc_info->hijacked_) {
        curr_iteration_ = rpc_info->hijacked_interceptor_;
      } else {
        curr_iteration_ = rpc_info->interceptors_.size() - 1;
      }
    }
    rpc_info->RunInterceptor(this, curr_iteration_);
  }

  void RunServerInterceptors() {
    auto* rpc_info = call_->server_rpc_info();
    if (!reverse_) {
      curr_iteration_ = 0;
    } else {
      curr_iteration_ = rpc_info->interceptors_.size() - 1;
    }
    rpc_info->RunInterceptor(this, curr_iteration_);
  }

  void ProceedClient() {
    auto* rpc_info = call_->client_rpc_info();
    if (rpc_info->hijacked_ && !reverse_ &&
        curr_iteration_ == rpc_info->hijacked_interceptor_ &&
        !ran_hijacking_interceptor_) {
      // We now need to provide hijacked recv ops to this interceptor
      ClearHookPoints();
      ops_->SetHijackingState();
      ran_hijacking_interceptor_ = true;
      rpc_info->RunInterceptor(this, curr_iteration_);
      return;
    }
    if (!reverse_) {
      curr_iteration_++;
      // We are going down the stack of interceptors
      if (curr_iteration_ < static_cast<long>(rpc_info->interceptors_.size())) {
        if (rpc_info->hijacked_ &&
            curr_iteration_ > rpc_info->hijacked_interceptor_) {
          // This is a hijacked RPC and we are done with hijacking
          ops_->ContinueFillOpsAfterInterception();
        } else {
          rpc_info->RunInterceptor(this, curr_iteration_);
        }
      } else {
        // we are done running all the interceptors without any hijacking
        ops_->ContinueFillOpsAfterInterception();
      }
    } else {
      curr_iteration_--;
      // We are going up the stack of interceptors
      if (curr_iteration_ >= 0) {
        // Continue running interceptors
        rpc_info->RunInterceptor(this, curr_iteration_);
      } else {
        // we are done running all the interceptors without any hijacking
        ops_->ContinueFinalizeResultAfterInterception();
      }
    }
  }

  void ProceedServer() {
    auto* rpc_info = call_->server_rpc_info();
    if (!reverse_) {
      curr_iteration_++;
      if (curr_iteration_ < static_cast<long>(rpc_info->interceptors_.size())) {
        return rpc_info->RunInterceptor(this, curr_iteration_);
      } else if (ops_) {
        return ops_->ContinueFillOpsAfterInterception();
      }
    } else {
      curr_iteration_--;
      // We are going up the stack of interceptors
      if (curr_iteration_ >= 0) {
        // Continue running interceptors
        return rpc_info->RunInterceptor(this, curr_iteration_);
      } else if (ops_) {
        return ops_->ContinueFinalizeResultAfterInterception();
      }
    }
    GPR_CODEGEN_ASSERT(callback_);
    callback_();
  }

  void ClearHookPoints() {
    for (auto i = 0;
         i < static_cast<int>(
                 experimental::InterceptionHookPoints::NUM_INTERCEPTION_HOOKS);
         i++) {
      hooks_[i] = false;
    }
  }

  std::array<bool,
             static_cast<int>(
                 experimental::InterceptionHookPoints::NUM_INTERCEPTION_HOOKS)>
      hooks_;

  int curr_iteration_ = 0;  // Current iterator
  bool reverse_ = false;
  bool ran_hijacking_interceptor_ = false;
  Call* call_ =
      nullptr;  // The Call object is present along with CallOpSet object
  CallOpSetInterface* ops_ = nullptr;
  std::function<void(void)> callback_;

  ByteBuffer* send_message_ = nullptr;

  std::multimap<grpc::string, grpc::string>* send_initial_metadata_;

  grpc_status_code* code_ = nullptr;
  grpc::string* error_details_ = nullptr;
  grpc::string* error_message_ = nullptr;
  Status send_status_;

  std::multimap<grpc::string, grpc::string>* send_trailing_metadata_ = nullptr;

  void* recv_message_ = nullptr;

  internal::MetadataMap* recv_initial_metadata_ = nullptr;

  Status* recv_status_ = nullptr;

  internal::MetadataMap* recv_trailing_metadata_ = nullptr;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_INTERCEPTOR_COMMON_H
