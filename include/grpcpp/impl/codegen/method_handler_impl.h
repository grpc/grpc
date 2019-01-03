/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_METHOD_HANDLER_IMPL_H
#define GRPCPP_IMPL_CODEGEN_METHOD_HANDLER_IMPL_H

#include <grpcpp/impl/codegen/byte_buffer.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/rpc_service_method.h>
#include <grpcpp/impl/codegen/sync_stream.h>

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
Status CatchingFunctionHandler(Callable&& handler) {
#if GRPC_ALLOW_EXCEPTIONS
  try {
    return handler();
  } catch (...) {
    return Status(StatusCode::UNKNOWN, "Unexpected error in RPC handling");
  }
#else   // GRPC_ALLOW_EXCEPTIONS
  return handler();
#endif  // GRPC_ALLOW_EXCEPTIONS
}

/// A wrapper class of an application provided rpc method handler.
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler : public MethodHandler {
 public:
  RpcMethodHandler(std::function<Status(ServiceType*, ServerContext*,
                                        const RequestType*, ResponseType*)>
                       func,
                   ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    ResponseType rsp;
    Status status = param.status;
    if (status.ok()) {
      status = CatchingFunctionHandler([this, &param, &rsp] {
        return func_(service_, param.server_context,
                     static_cast<RequestType*>(param.request), &rsp);
      });
      static_cast<RequestType*>(param.request)->~RequestType();
    }

    GPR_CODEGEN_ASSERT(!param.server_context->sent_initial_metadata_);
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        ops;
    ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                            param.server_context->initial_metadata_flags());
    if (param.server_context->compression_level_set()) {
      ops.set_compression_level(param.server_context->compression_level());
    }
    if (status.ok()) {
      status = ops.SendMessagePtr(&rsp);
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
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
  /// Application provided rpc handler function.
  std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                       ResponseType*)>
      func_;
  // The class the above handler function lives in.
  ServiceType* service_;
};

/// A wrapper class of an application provided client streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler : public MethodHandler {
 public:
  ClientStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*,
                           ServerReader<RequestType>*, ResponseType*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    ServerReader<RequestType> reader(param.call, param.server_context);
    ResponseType rsp;
    Status status = CatchingFunctionHandler([this, &param, &reader, &rsp] {
      return func_(service_, param.server_context, &reader, &rsp);
    });

    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
    }
    if (status.ok()) {
      status = ops.SendMessagePtr(&rsp);
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*, ServerReader<RequestType>*,
                       ResponseType*)>
      func_;
  ServiceType* service_;
};

/// A wrapper class of an application provided server streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler : public MethodHandler {
 public:
  ServerStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                           ServerWriter<ResponseType>*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    Status status = param.status;
    if (status.ok()) {
      ServerWriter<ResponseType> writer(param.call, param.server_context);
      status = CatchingFunctionHandler([this, &param, &writer] {
        return func_(service_, param.server_context,
                     static_cast<RequestType*>(param.request), &writer);
      });
      static_cast<RequestType*>(param.request)->~RequestType();
    }

    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    if (param.server_context->has_pending_ops_) {
      param.call->cq()->Pluck(&param.server_context->pending_ops_);
    }
    param.call->cq()->Pluck(&ops);
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
  std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                       ServerWriter<ResponseType>*)>
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
class TemplatedBidiStreamingHandler : public MethodHandler {
 public:
  TemplatedBidiStreamingHandler(
      std::function<Status(ServerContext*, Streamer*)> func)
      : func_(func), write_needed_(WriteNeeded) {}

  void RunHandler(const HandlerParameter& param) final {
    Streamer stream(param.call, param.server_context);
    Status status = CatchingFunctionHandler([this, &param, &stream] {
      return func_(param.server_context, &stream);
    });

    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(&param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
      if (write_needed_ && status.ok()) {
        // If we needed a write but never did one, we need to mark the
        // status as a fail
        status = Status(StatusCode::INTERNAL,
                        "Service did not provide response message");
      }
    }
    ops.ServerSendStatus(&param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    if (param.server_context->has_pending_ops_) {
      param.call->cq()->Pluck(&param.server_context->pending_ops_);
    }
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServerContext*, Streamer*)> func_;
  const bool write_needed_;
};

template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler
    : public TemplatedBidiStreamingHandler<
          ServerReaderWriter<ResponseType, RequestType>, false> {
 public:
  BidiStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*,
                           ServerReaderWriter<ResponseType, RequestType>*)>
          func,
      ServiceType* service)
      : TemplatedBidiStreamingHandler<
            ServerReaderWriter<ResponseType, RequestType>, false>(std::bind(
            func, service, std::placeholders::_1, std::placeholders::_2)) {}
};

template <class RequestType, class ResponseType>
class StreamedUnaryHandler
    : public TemplatedBidiStreamingHandler<
          ServerUnaryStreamer<RequestType, ResponseType>, true> {
 public:
  explicit StreamedUnaryHandler(
      std::function<Status(ServerContext*,
                           ServerUnaryStreamer<RequestType, ResponseType>*)>
          func)
      : TemplatedBidiStreamingHandler<
            ServerUnaryStreamer<RequestType, ResponseType>, true>(func) {}
};

template <class RequestType, class ResponseType>
class SplitServerStreamingHandler
    : public TemplatedBidiStreamingHandler<
          ServerSplitStreamer<RequestType, ResponseType>, false> {
 public:
  explicit SplitServerStreamingHandler(
      std::function<Status(ServerContext*,
                           ServerSplitStreamer<RequestType, ResponseType>*)>
          func)
      : TemplatedBidiStreamingHandler<
            ServerSplitStreamer<RequestType, ResponseType>, false>(func) {}
};

/// General method handler class for errors that prevent real method use
/// e.g., handle unknown method by returning UNIMPLEMENTED error.
template <StatusCode code>
class ErrorMethodHandler : public MethodHandler {
 public:
  template <class T>
  static void FillOps(ServerContext* context, T* ops) {
    Status status(code, "");
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
    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    FillOps(param.server_context, &ops);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    Status* status) final {
    // We have to destroy any request payload
    if (req != nullptr) {
      g_core_codegen_interface->grpc_byte_buffer_destroy(req);
    }
    return nullptr;
  }
};

typedef ErrorMethodHandler<StatusCode::UNIMPLEMENTED> UnknownMethodHandler;
typedef ErrorMethodHandler<StatusCode::RESOURCE_EXHAUSTED>
    ResourceExhaustedHandler;

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_METHOD_HANDLER_IMPL_H
