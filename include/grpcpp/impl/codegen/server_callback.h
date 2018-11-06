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

#ifndef GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_H
#define GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_H

#include <functional>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/call_op_set.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/server_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {

// forward declarations
namespace internal {
template <class ServiceType, class RequestType, class ResponseType>
class CallbackUnaryHandler;
}  // namespace internal

namespace experimental {

// For unary RPCs, the exposed controller class is only an interface
// and the actual implementation is an internal class.
class ServerCallbackRpcController {
 public:
  virtual ~ServerCallbackRpcController() {}

  // The method handler must call this function when it is done so that
  // the library knows to free its resources
  virtual void Finish(Status s) = 0;

  // Allow the method handler to push out the initial metadata before
  // the response and status are ready
  virtual void SendInitialMetadata(std::function<void(bool)>) = 0;
};

}  // namespace experimental

namespace internal {

template <class ServiceType, class RequestType, class ResponseType>
class CallbackUnaryHandler : public MethodHandler {
 public:
  CallbackUnaryHandler(
      std::function<void(ServerContext*, const RequestType*, ResponseType*,
                         experimental::ServerCallbackRpcController*)>
          func,
      ServiceType* service)
      : func_(func) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a controller structure (that includes request/response)
    g_core_codegen_interface->grpc_call_ref(param.call->call());
    auto* controller = new (g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackRpcControllerImpl)))
        ServerCallbackRpcControllerImpl(
            param.server_context, param.call,
            static_cast<RequestType*>(param.request),
            std::move(param.call_requester));
    Status status = param.status;

    if (status.ok()) {
      // Call the actual function handler and expect the user to call finish
      CatchingCallback(std::move(func_), param.server_context,
                       controller->request(), controller->response(),
                       controller);
    } else {
      // if deserialization failed, we need to fail the call
      controller->Finish(status);
    }
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    Status* status) final {
    ByteBuffer buf;
    buf.set_buffer(req);
    auto* request = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call, sizeof(RequestType))) RequestType();
    *status = SerializationTraits<RequestType>::Deserialize(&buf, request);
    buf.Release();
    if (status->ok()) {
      return request;
    }
    request->~RequestType();
    return nullptr;
  }

 private:
  std::function<void(ServerContext*, const RequestType*, ResponseType*,
                     experimental::ServerCallbackRpcController*)>
      func_;

  // The implementation class of ServerCallbackRpcController is a private member
  // of CallbackUnaryHandler since it is never exposed anywhere, and this allows
  // it to take advantage of CallbackUnaryHandler's friendships.
  class ServerCallbackRpcControllerImpl
      : public experimental::ServerCallbackRpcController {
   public:
    void Finish(Status s) override {
      finish_tag_.Set(
          call_.call(),
          [this](bool) {
            grpc_call* call = call_.call();
            auto call_requester = std::move(call_requester_);
            this->~ServerCallbackRpcControllerImpl();  // explicitly call
                                                       // destructor
            g_core_codegen_interface->grpc_call_unref(call);
            call_requester();
          },
          &finish_buf_);
      if (!ctx_->sent_initial_metadata_) {
        finish_buf_.SendInitialMetadata(&ctx_->initial_metadata_,
                                        ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          finish_buf_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      // The response is dropped if the status is not OK.
      if (s.ok()) {
        finish_buf_.ServerSendStatus(&ctx_->trailing_metadata_,
                                     finish_buf_.SendMessage(resp_));
      } else {
        finish_buf_.ServerSendStatus(&ctx_->trailing_metadata_, s);
      }
      finish_buf_.set_core_cq_tag(&finish_tag_);
      call_.PerformOps(&finish_buf_);
    }

    void SendInitialMetadata(std::function<void(bool)> f) override {
      GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

      meta_tag_.Set(call_.call(), std::move(f), &meta_buf_);
      meta_buf_.SendInitialMetadata(&ctx_->initial_metadata_,
                                    ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        meta_buf_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
      meta_buf_.set_core_cq_tag(&meta_tag_);
      call_.PerformOps(&meta_buf_);
    }

   private:
    template <class SrvType, class ReqType, class RespType>
    friend class CallbackUnaryHandler;

    ServerCallbackRpcControllerImpl(ServerContext* ctx, Call* call,
                                    RequestType* req,
                                    std::function<void()> call_requester)
        : ctx_(ctx),
          call_(*call),
          req_(req),
          call_requester_(std::move(call_requester)) {}

    ~ServerCallbackRpcControllerImpl() { req_->~RequestType(); }

    RequestType* request() { return req_; }
    ResponseType* response() { return &resp_; }

    CallOpSet<CallOpSendInitialMetadata> meta_buf_;
    CallbackWithSuccessTag meta_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        finish_buf_;
    CallbackWithSuccessTag finish_tag_;

    ServerContext* ctx_;
    Call call_;
    RequestType* req_;
    ResponseType resp_;
    std::function<void()> call_requester_;
  };
};

}  // namespace internal

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_H
