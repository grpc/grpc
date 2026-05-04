//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPCPP_SUPPORT_METHOD_HANDLER_H
#define GRPCPP_SUPPORT_METHOD_HANDLER_H

#include <grpc/byte_buffer.h>
#include <grpcpp/impl/rpc_service_method.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/sync_stream.h>

#include <cstdlib>

#include "absl/log/absl_check.h"
#include "absl/log/log.h"

namespace grpc {

namespace internal {

// Invoke the method handler, fill in the status, and
// return whether or not we finished safely (without an exception).
// Note that exception handling is 0-cost in most compiler/library
// implementations (except when an exception is actually thrown),
// so this process doesn't require additional overhead in the common case.
// Additionally, we don't need to return if we caught an exception or not;
// the handling is the same in either case.
template <class Callable>
::grpc::Status CatchingFunctionHandler(Callable&& handler) {
#if GRPC_ALLOW_EXCEPTIONS
  try {
    return handler();
  } catch (...) {
    return grpc::Status(grpc::StatusCode::UNKNOWN,
                        "Unexpected error in RPC handling");
  }
#else   // GRPC_ALLOW_EXCEPTIONS
  return handler();
#endif  // GRPC_ALLOW_EXCEPTIONS
}

class ExtraRequestsCheckTag : public grpc::internal::CompletionQueueTag {
 public:
  explicit ExtraRequestsCheckTag(grpc_byte_buffer** extra_msg)
      : extra_msg_(extra_msg) {}
  bool FinalizeResult(void** /*tag*/, bool* /*status*/) override {
    Cleanup();
    return false;
  }
  void Cleanup() {
    if (*extra_msg_ != nullptr) grpc_byte_buffer_destroy(*extra_msg_);
    delete extra_msg_;
    delete this;
  }

 private:
  grpc_byte_buffer** extra_msg_;
};

inline void* CheckForExtraRequests(grpc::internal::Call* call,
                                   grpc::Status* status,
                                   const char* error_message) {
  if (!status->ok()) return nullptr;
  grpc_byte_buffer** extra_msg = new grpc_byte_buffer*(nullptr);
  auto* tag = new ExtraRequestsCheckTag(extra_msg);
  grpc_op op;
  op.op = GRPC_OP_RECV_MESSAGE;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.recv_message.recv_message = extra_msg;
  if (grpc_call_start_batch(call->call(), &op, 1, tag, nullptr) !=
      GRPC_CALL_OK) {
    delete extra_msg;
    delete tag;
    return nullptr;
  }
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                       gpr_time_from_millis(10, GPR_TIMESPAN));
  ABSL_LOG(INFO) << "CheckForExtraRequests: plucking with 10ms deadline";
  grpc_event ev =
      grpc_completion_queue_pluck(call->cq()->cq(), tag, deadline, nullptr);
  ABSL_LOG(INFO) << "CheckForExtraRequests: pluck returned event type "
                 << ev.type;
  if (ev.type != GRPC_QUEUE_TIMEOUT) {
    if (*extra_msg != nullptr) {
      ABSL_LOG(INFO) << "CheckForExtraRequests: found extra message";
      *status = grpc::Status(grpc::StatusCode::UNIMPLEMENTED, error_message);
      grpc_byte_buffer_destroy(*extra_msg);
    }
    delete extra_msg;
    delete tag;
    return nullptr;
  }
  ABSL_LOG(INFO) << "CheckForExtraRequests: timed out, returning pending tag";
  return tag;
}

/// A helper function with reduced templating to do the common work needed to
/// actually send the server response. Uses non-const parameter for Status since
/// this should only ever be called from the end of the RunHandler method.

template <class ResponseType>
void UnaryRunHandlerHelper(const MethodHandler::HandlerParameter& param,
                           ResponseType* rsp, grpc::Status& status) {
  ABSL_CHECK(!param.server_context->sent_initial_metadata_);
  grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                            grpc::internal::CallOpSendMessage,
                            grpc::internal::CallOpServerSendStatus>
      ops;
  ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                          param.server_context->initial_metadata_flags());
  if (param.server_context->compression_level_set()) {
    ops.set_compression_level(param.server_context->compression_level());
  }
  if (status.ok()) {
    status = ops.SendMessagePtr(rsp, param.server_context->memory_allocator());
  }
  ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
  ops.FillOps(param.call);
  param.call->cq()->Pluck(&ops);
}

/// A helper function with reduced templating to do deserializing.

template <class RequestType>
void* UnaryDeserializeHelper(grpc_byte_buffer* req, grpc::Status* status,
                             RequestType* request) {
  grpc::ByteBuffer buf;
  buf.set_buffer(req);
  *status = grpc::Deserialize(&buf, static_cast<RequestType*>(request));
  buf.Release();
  if (status->ok()) {
    return request;
  }
  request->~RequestType();
  return nullptr;
}

/// A wrapper class of an application provided rpc method handler.
template <class ServiceType, class RequestType, class ResponseType,
          class BaseRequestType = RequestType,
          class BaseResponseType = ResponseType>
class RpcMethodHandler : public grpc::internal::MethodHandler {
 public:
  RpcMethodHandler(
      std::function<grpc::Status(ServiceType*, grpc::ServerContext*,
                                 const RequestType*, ResponseType*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    ResponseType rsp;
    grpc::Status status = param.status;
    void* pending_tag = nullptr;
    if (status.ok()) {
      status = CatchingFunctionHandler([this, &param, &rsp] {
        return func_(service_,
                     static_cast<grpc::ServerContext*>(param.server_context),
                     static_cast<RequestType*>(param.request), &rsp);
      });
      static_cast<RequestType*>(param.request)->~RequestType();
      pending_tag = CheckForExtraRequests(
          param.call, &status,
          "Cardinality violation: Unary method received extra request");
    }
    UnaryRunHandlerHelper(param, static_cast<BaseResponseType*>(&rsp), status);
    if (pending_tag != nullptr) {
      grpc_call_cancel(param.call->call(), nullptr);
      gpr_timespec pluck_deadline = gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(100, GPR_TIMESPAN));
      grpc_event ev = grpc_completion_queue_pluck(
          param.call->cq()->cq(), pending_tag, pluck_deadline, nullptr);
      if (ev.type != GRPC_QUEUE_TIMEOUT) {
        static_cast<ExtraRequestsCheckTag*>(pending_tag)->Cleanup();
      } else {
        ABSL_LOG(ERROR)
            << "CheckForExtraRequests cleanup timed out, leaking tag";
      }
    }
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    grpc::Status* status, void** /*handler_data*/) final {
    auto* request =
        new (grpc_call_arena_alloc(call, sizeof(RequestType))) RequestType;
    return UnaryDeserializeHelper(req, status,
                                  static_cast<BaseRequestType*>(request));
  }

 private:
  /// Application provided rpc handler function.
  std::function<grpc::Status(ServiceType*, grpc::ServerContext*,
                             const RequestType*, ResponseType*)>
      func_;
  // The class the above handler function lives in.
  ServiceType* service_;
};

/// A wrapper class of an application provided client streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler : public grpc::internal::MethodHandler {
 public:
  ClientStreamingHandler(
      std::function<grpc::Status(ServiceType*, grpc::ServerContext*,
                                 ServerReader<RequestType>*, ResponseType*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    ServerReader<RequestType> reader(
        param.call, static_cast<grpc::ServerContext*>(param.server_context));
    ResponseType rsp;
    grpc::Status status =
        CatchingFunctionHandler([this, &param, &reader, &rsp] {
          return func_(service_,
                       static_cast<grpc::ServerContext*>(param.server_context),
                       &reader, &rsp);
        });

    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage,
                              grpc::internal::CallOpServerSendStatus>
        ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
    }
    if (status.ok()) {
      status =
          ops.SendMessagePtr(&rsp, param.server_context->memory_allocator());
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    ops.FillOps(param.call);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<grpc::Status(ServiceType*, grpc::ServerContext*,
                             ServerReader<RequestType>*, ResponseType*)>
      func_;
  ServiceType* service_;
};

/// A wrapper class of an application provided server streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler : public grpc::internal::MethodHandler {
 public:
  ServerStreamingHandler(std::function<grpc::Status(
                             ServiceType*, grpc::ServerContext*,
                             const RequestType*, ServerWriter<ResponseType>*)>
                             func,
                         ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    grpc::Status status = param.status;
    void* pending_tag = nullptr;
    if (status.ok()) {
      ServerWriter<ResponseType> writer(
          param.call, static_cast<grpc::ServerContext*>(param.server_context));
      status = CatchingFunctionHandler([this, &param, &writer] {
        return func_(service_,
                     static_cast<grpc::ServerContext*>(param.server_context),
                     static_cast<RequestType*>(param.request), &writer);
      });
      static_cast<RequestType*>(param.request)->~RequestType();
      pending_tag = CheckForExtraRequests(
          param.call, &status,
          "Cardinality violation: Server-streaming method "
          "received extra request");
    }
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpServerSendStatus>
        ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    ops.FillOps(param.call);
    if (param.server_context->has_pending_ops_) {
      param.call->cq()->Pluck(&param.server_context->pending_ops_);
    }
    param.call->cq()->Pluck(&ops);
    if (pending_tag != nullptr) {
      grpc_call_cancel(param.call->call(), nullptr);
      gpr_timespec pluck_deadline = gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(100, GPR_TIMESPAN));
      grpc_event ev = grpc_completion_queue_pluck(
          param.call->cq()->cq(), pending_tag, pluck_deadline, nullptr);
      if (ev.type != GRPC_QUEUE_TIMEOUT) {
        static_cast<ExtraRequestsCheckTag*>(pending_tag)->Cleanup();
      } else {
        ABSL_LOG(ERROR)
            << "CheckForExtraRequests cleanup timed out, leaking tag";
      }
    }
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    grpc::Status* status, void** /*handler_data*/) final {
    grpc::ByteBuffer buf;
    buf.set_buffer(req);
    auto* request =
        new (grpc_call_arena_alloc(call, sizeof(RequestType))) RequestType();
    *status = grpc::Deserialize(&buf, request);
    buf.Release();
    if (status->ok()) {
      return request;
    }
    request->~RequestType();
    return nullptr;
  }

 private:
  std::function<grpc::Status(ServiceType*, grpc::ServerContext*,
                             const RequestType*, ServerWriter<ResponseType>*)>
      func_;
  ServiceType* service_;
};

/// A wrapper class of an application provided bidi-streaming handler.
/// This also applies to server-streamed implementation of a unary method
/// with the additional requirement that such methods must have done a
/// write for status to be ok
/// Since this is used by more than 1 class, the service is not passed in.
/// Instead, it is expected to be an implicitly-captured argument of func
/// (through bind or something along those lines)
template <class Streamer, bool WriteNeeded>
class TemplatedBidiStreamingHandler : public grpc::internal::MethodHandler {
 public:
  explicit TemplatedBidiStreamingHandler(
      std::function<grpc::Status(grpc::ServerContext*, Streamer*)> func)
      : func_(func), write_needed_(WriteNeeded) {}

  void RunHandler(const HandlerParameter& param) final {
    Streamer stream(param.call,
                    static_cast<grpc::ServerContext*>(param.server_context));
    grpc::Status status = CatchingFunctionHandler([this, &param, &stream] {
      return func_(static_cast<grpc::ServerContext*>(param.server_context),
                   &stream);
    });

    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpServerSendStatus>
        ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
      if (write_needed_ && status.ok()) {
        // If we needed a write but never did one, we need to mark the
        // status as a fail
        status = grpc::Status(grpc::StatusCode::INTERNAL,
                              "Service did not provide response message");
      }
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    ops.FillOps(param.call);
    if (param.server_context->has_pending_ops_) {
      param.call->cq()->Pluck(&param.server_context->pending_ops_);
    }
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<grpc::Status(grpc::ServerContext*, Streamer*)> func_;
  const bool write_needed_;
};

template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler
    : public TemplatedBidiStreamingHandler<
          ServerReaderWriter<ResponseType, RequestType>, false> {
 public:
  BidiStreamingHandler(std::function<grpc::Status(
                           ServiceType*, grpc::ServerContext*,
                           ServerReaderWriter<ResponseType, RequestType>*)>
                           func,
                       ServiceType* service)
      // TODO(vjpai): When gRPC supports C++14, move-capture func in the below
      : TemplatedBidiStreamingHandler<
            ServerReaderWriter<ResponseType, RequestType>, false>(
            [func, service](
                grpc::ServerContext* ctx,
                ServerReaderWriter<ResponseType, RequestType>* streamer) {
              return func(service, ctx, streamer);
            }) {}
};

template <class RequestType, class ResponseType>
class StreamedUnaryHandler
    : public TemplatedBidiStreamingHandler<
          ServerUnaryStreamer<RequestType, ResponseType>, true> {
 public:
  explicit StreamedUnaryHandler(
      std::function<
          grpc::Status(grpc::ServerContext*,
                       ServerUnaryStreamer<RequestType, ResponseType>*)>
          func)
      : TemplatedBidiStreamingHandler<
            ServerUnaryStreamer<RequestType, ResponseType>, true>(
            std::move(func)) {}
};

template <class RequestType, class ResponseType>
class SplitServerStreamingHandler
    : public TemplatedBidiStreamingHandler<
          ServerSplitStreamer<RequestType, ResponseType>, false> {
 public:
  explicit SplitServerStreamingHandler(
      std::function<
          grpc::Status(grpc::ServerContext*,
                       ServerSplitStreamer<RequestType, ResponseType>*)>
          func)
      : TemplatedBidiStreamingHandler<
            ServerSplitStreamer<RequestType, ResponseType>, false>(
            std::move(func)) {}
};

/// General method handler class for errors that prevent real method use
/// e.g., handle unknown method by returning UNIMPLEMENTED error.
template <grpc::StatusCode code>
class ErrorMethodHandler : public grpc::internal::MethodHandler {
 public:
  explicit ErrorMethodHandler(const std::string& message) : message_(message) {}

  template <class T>
  static void FillOps(grpc::ServerContextBase* context,
                      const std::string& message, T* ops) {
    grpc::Status status(code, message);
    if (!context->sent_initial_metadata_) {
      ops->SendInitialMetadata(&context->initial_metadata_,
                               context->initial_metadata_flags());
      if (context->compression_level_set()) {
        ops->set_compression_level(context->compression_level());
      }
      context->sent_initial_metadata_ = true;
    }
    ops->ServerSendStatus(&context->trailing_metadata_, status);
  }

  void RunHandler(const HandlerParameter& param) final {
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpServerSendStatus>
        ops;
    FillOps(param.server_context, message_, &ops);
    ops.FillOps(param.call);
    param.call->cq()->Pluck(&ops);
  }

  void* Deserialize(grpc_call* /*call*/, grpc_byte_buffer* req,
                    grpc::Status* /*status*/, void** /*handler_data*/) final {
    // We have to destroy any request payload
    if (req != nullptr) {
      grpc_byte_buffer_destroy(req);
    }
    return nullptr;
  }

 private:
  const std::string message_;
};

typedef ErrorMethodHandler<grpc::StatusCode::UNIMPLEMENTED>
    UnknownMethodHandler;
typedef ErrorMethodHandler<grpc::StatusCode::RESOURCE_EXHAUSTED>
    ResourceExhaustedHandler;

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_SUPPORT_METHOD_HANDLER_H
