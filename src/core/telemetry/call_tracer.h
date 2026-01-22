//
//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_TELEMETRY_CALL_TRACER_H
#define GRPC_SRC_CORE_TELEMETRY_CALL_TRACER_H

#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include <memory>
#include <string>
#include <variant>

#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/ref_counted_string.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// 🚨🚨🚨 REFACTORING IN PROGRESS 🚨🚨🚨
//
// There are significant changes in flight for this file.
// It's worth checking in with ctiller before making substantial changes.
//
// General theme: we're moving to a concrete set of CallTracer types, and
// thinning the interface down - the result will be more commonality between
// tracer implementations, and less indirect calls out to the tracers.

// Inheritance hierarchy for concrete types:
//
// CallSpan (wraps CallTracerAnnotationInterface)
//   |
//   +-- CallTracer (wraps CallTracerInterface)
//   |   |
//   |   +-- CallAttemptTracer
//   |   |   (wraps ClientCallTracerInterface::CallAttemptTracer)
//   |   |
//   |   +-- ServerCallTracer (wraps ServerCallTracerInterface)
//   |
//   +-- ClientCallTracer (wraps ClientCallTracerInterface)

// The base class for all tracer implementations.
class CallTracerAnnotationInterface {
 public:
  // Enum associated with types of Annotations.
  enum class AnnotationType {
    kMetadataSizes,
    kHttpTransport,
    kSendInitialMetadata,
    kDoNotUse_MustBeLast,
  };

  // Base class to define a new type of annotation.
  class Annotation {
   public:
    using ValueType = std::variant<bool, int64_t, double, absl::string_view>;

    explicit Annotation(AnnotationType type) : type_(type) {}
    AnnotationType type() const { return type_; }
    virtual std::string ToString() const = 0;
    virtual void ForEachKeyValue(
        absl::FunctionRef<void(absl::string_view, ValueType)>) const = 0;
    virtual ~Annotation() = default;

   private:
    const AnnotationType type_;
  };

  virtual ~CallTracerAnnotationInterface() {}
  // Records an annotation on the call attempt.
  // TODO(yashykt): If needed, extend this to attach attributes with
  // annotations.
  virtual void RecordAnnotation(absl::string_view annotation) = 0;
  virtual void RecordAnnotation(const Annotation& annotation) = 0;
  virtual std::string TraceId() = 0;
  virtual std::string SpanId() = 0;
  virtual bool IsSampled() = 0;
  // Indicates whether this tracer is a delegating tracer or not.
  // `DelegatingClientCallTracer`, `DelegatingClientCallAttemptTracer` and
  // `DelegatingServerCallTracer` are the only delegating call tracers.
  virtual bool IsDelegatingTracer() { return false; }
};

class SendInitialMetadataAnnotation final
    : public CallTracerAnnotationInterface::Annotation {
 public:
  explicit SendInitialMetadataAnnotation(grpc_metadata_batch* metadata)
      : Annotation(CallTracerAnnotationInterface::AnnotationType::
                       kSendInitialMetadata),
        metadata_(metadata) {}
  const grpc_metadata_batch* metadata() const { return metadata_; }
  std::string ToString() const override;
  void ForEachKeyValue(
      absl::FunctionRef<void(absl::string_view, ValueType)> f) const override;

 private:
  const grpc_metadata_batch* metadata_;
};

// The base class for CallAttemptTracer and ServerCallTracer.
// TODO(yashykt): What's a better name for this?
class CallTracerInterface : public CallTracerAnnotationInterface {
 public:
  ~CallTracerInterface() override {}
  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
  virtual void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) = 0;
  virtual void MutateSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) = 0;
  virtual void RecordSendTrailingMetadata(
      grpc_metadata_batch* send_trailing_metadata) = 0;
  virtual void RecordSendMessage(const Message& send_message) = 0;
  // Only invoked if message was actually compressed.
  virtual void RecordSendCompressedMessage(
      const Message& send_compressed_message) = 0;
  // The `RecordReceivedInitialMetadata()` and `RecordReceivedMessage()`
  // methods should only be invoked when the metadata/message was
  // successfully received, i.e., without any error.
  virtual void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata) = 0;
  virtual void RecordReceivedMessage(const Message& recv_message) = 0;
  // Only invoked if message was actually decompressed.
  virtual void RecordReceivedDecompressedMessage(
      const Message& recv_decompressed_message) = 0;
  virtual void RecordCancel(grpc_error_handle cancel_error) = 0;

  struct TransportByteSize {
    uint64_t framing_bytes = 0;
    uint64_t data_bytes = 0;
    uint64_t header_bytes = 0;

    TransportByteSize& operator+=(const TransportByteSize& other);
  };
  virtual void RecordIncomingBytes(
      const TransportByteSize& transport_byte_size) = 0;
  virtual void RecordOutgoingBytes(
      const TransportByteSize& transport_byte_size) = 0;

  // Traces a new TCP transport attempt for this call attempt. Note the TCP
  // transport may finish tracing and unref the TCP tracer before or after the
  // call completion in gRPC core. No TCP tracing when null is returned.
  virtual std::shared_ptr<TcpCallTracer> StartNewTcpTrace() = 0;
};

class ClientCallTracerInterface;
class ServerCallTracerInterface;

// Interface for a tracer that records activities on a call. Actual attempts for
// this call are traced with CallAttemptTracer after invoking RecordNewAttempt()
// on the ClientCallTracer object.
class ClientCallTracerInterface : public CallTracerAnnotationInterface {
 public:
  // Interface for a tracer that records activities on a particular call
  // attempt.
  // (A single RPC can have multiple attempts due to retry/hedging policies or
  // as transparent retry attempts.)
  class CallAttemptTracer : public CallTracerInterface {
   public:
    // Note that not all of the optional label keys are exposed as public API.
    enum class OptionalLabelKey : std::uint8_t {
      kXdsServiceName,       // not public
      kXdsServiceNamespace,  // not public
      kLocality,
      kBackendService,
      kSize  // should be last
    };

    ~CallAttemptTracer() override {}

    // TODO(yashykt): The following two methods `RecordReceivedTrailingMetadata`
    // and `RecordEnd` should be moved into CallTracerInterface.
    // If the call was cancelled before the recv_trailing_metadata op
    // was started, recv_trailing_metadata and transport_stream_stats
    // will be null.
    virtual void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        // TODO(roth): Remove this argument when the
        // call_tracer_in_transport experiment finishes rolling out.
        const grpc_transport_stream_stats* transport_stream_stats) = 0;
    // Should be the last API call to the object. Once invoked, the tracer
    // library is free to destroy the object.
    virtual void RecordEnd() = 0;

    // Sets an optional label on the per-attempt metrics recorded at the end of
    // the attempt.
    virtual void SetOptionalLabel(OptionalLabelKey key,
                                  RefCountedStringValue value) = 0;
  };

  ~ClientCallTracerInterface() override {}

  // Records a new attempt for the associated call. \a transparent denotes
  // whether the attempt is being made as a transparent retry or as a
  // non-transparent retry/hedging attempt. (There will be at least one attempt
  // even if the call is not being retried.) The `ClientCallTracerInterface`
  // object retains ownership to the newly created `CallAttemptTracer` object.
  // RecordEnd() serves as an indication that the call stack is done with all
  // API calls, and the tracer library is free to destroy it after that.
  virtual CallAttemptTracer* StartNewAttempt(bool is_transparent_retry) = 0;
};

// Interface for a tracer that records activities on a server call.
class ServerCallTracerInterface : public CallTracerInterface {
 public:
  ~ServerCallTracerInterface() override {}
  // TODO(yashykt): The following two methods `RecordReceivedTrailingMetadata`
  // and `RecordEnd` should be moved into CallTracerInterface.
  virtual void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* recv_trailing_metadata) = 0;
  // Should be the last API call to the object. Once invoked, the tracer
  // library is free to destroy the object.
  virtual void RecordEnd(const grpc_call_final_info* final_info) = 0;
};

// Interface for a factory that can create a ServerCallTracerInterface object
// per server call.
class ServerCallTracerFactory {
 public:
  struct RawPointerChannelArgTag {};

  virtual ~ServerCallTracerFactory() {}

  virtual ServerCallTracerInterface* CreateNewServerCallTracer(
      Arena* arena, const ChannelArgs& channel_args) = 0;

  // Returns true if a server is to be traced, false otherwise.
  virtual bool IsServerTraced(const ChannelArgs& /*args*/) { return true; }

  // Use this method to get the server call tracer factory from channel args,
  // instead of directly fetching it with `GetObject`.
  static ServerCallTracerFactory* Get(const ChannelArgs& channel_args);

  // Registers a global ServerCallTracerFactory that will be used by default if
  // no corresponding channel arg was found. It is only valid to call this
  // before grpc_init(). It is the responsibility of the caller to maintain
  // this for the lifetime of the process.
  static void RegisterGlobal(ServerCallTracerFactory* factory);

  // Deletes any previous registered ServerCallTracerFactory.
  static void TestOnlyReset();

  static absl::string_view ChannelArgName();
};

// Concrete class for a call span.
// Wraps a CallTracerAnnotationInterface.
class CallSpan {
 public:
  explicit CallSpan(CallTracerAnnotationInterface* interface)
      : interface_(interface) {}

  void RecordAnnotation(absl::string_view annotation) {
    interface_->RecordAnnotation(annotation);
  }
  void RecordAnnotation(
      const CallTracerAnnotationInterface::Annotation& annotation) {
    interface_->RecordAnnotation(annotation);
  }
  std::string TraceId() { return interface_->TraceId(); }
  std::string SpanId() { return interface_->SpanId(); }
  bool IsSampled() { return interface_->IsSampled(); }

  CallTracerAnnotationInterface* span_impl() { return interface_; }

 protected:
  ~CallSpan() = default;

 private:
#ifndef NDEBUG
  virtual void VirtualMethodToEnsureDownCastBuildsInDebug() {}
#endif

  CallTracerAnnotationInterface* interface_;
};

// Concrete class for a call tracer.
// Wraps a CallTracerInterface.
class CallTracer : public CallSpan {
 public:
  explicit CallTracer(CallTracerInterface* interface)
      : CallSpan(interface), interface_(interface) {}

  void RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata);
  void RecordSendTrailingMetadata(grpc_metadata_batch* send_trailing_metadata) {
    interface_->RecordSendTrailingMetadata(send_trailing_metadata);
  }
  void RecordSendMessage(const Message& send_message) {
    interface_->RecordSendMessage(send_message);
  }
  void RecordSendCompressedMessage(const Message& send_compressed_message) {
    interface_->RecordSendCompressedMessage(send_compressed_message);
  }
  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata) {
    interface_->RecordReceivedInitialMetadata(recv_initial_metadata);
  }
  void RecordReceivedMessage(const Message& recv_message) {
    interface_->RecordReceivedMessage(recv_message);
  }
  void RecordReceivedDecompressedMessage(
      const Message& recv_decompressed_message) {
    interface_->RecordReceivedDecompressedMessage(recv_decompressed_message);
  }
  void RecordCancel(grpc_error_handle cancel_error) {
    interface_->RecordCancel(cancel_error);
  }
  void RecordIncomingBytes(
      const CallTracerInterface::TransportByteSize& transport_byte_size) {
    interface_->RecordIncomingBytes(transport_byte_size);
  }
  void RecordOutgoingBytes(
      const CallTracerInterface::TransportByteSize& transport_byte_size) {
    interface_->RecordOutgoingBytes(transport_byte_size);
  }
  std::shared_ptr<TcpCallTracer> StartNewTcpTrace() {
    return interface_->StartNewTcpTrace();
  }

 protected:
  ~CallTracer() = default;

 private:
  CallTracerInterface* interface_;
};

// Concrete class for a client call tracer.
// Wraps a ClientCallTracerInterface.
class ClientCallTracer final : public CallSpan {
 public:
  explicit ClientCallTracer(ClientCallTracerInterface* interface)
      : CallSpan(interface), interface_(interface) {}

  ClientCallTracerInterface::CallAttemptTracer* StartNewAttempt(
      bool is_transparent_retry) {
    return interface_->StartNewAttempt(is_transparent_retry);
  }

 private:
  ClientCallTracerInterface* interface_;
};

// Concrete class for a client call attempt tracer.
// Wraps a ClientCallTracerInterface::CallAttemptTracer.
class CallAttemptTracer final : public CallTracer {
 public:
  explicit CallAttemptTracer(
      ClientCallTracerInterface::CallAttemptTracer* interface)
      : CallTracer(interface), interface_(interface) {}

  void RecordReceivedTrailingMetadata(
      absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
      const grpc_transport_stream_stats* transport_stream_stats) {
    interface_->RecordReceivedTrailingMetadata(status, recv_trailing_metadata,
                                               transport_stream_stats);
  }
  void RecordEnd() { interface_->RecordEnd(); }
  void SetOptionalLabel(
      ClientCallTracerInterface::CallAttemptTracer::OptionalLabelKey key,
      RefCountedStringValue value) {
    interface_->SetOptionalLabel(key, value);
  }

 private:
  ClientCallTracerInterface::CallAttemptTracer* interface_;
};

// Concrete class for a server call tracer.
// Wraps a ServerCallTracerInterface.
class ServerCallTracer final : public CallTracer {
 public:
  explicit ServerCallTracer(ServerCallTracerInterface* interface)
      : CallTracer(interface), interface_(interface) {}

  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* recv_trailing_metadata) {
    interface_->RecordReceivedTrailingMetadata(recv_trailing_metadata);
  }
  void RecordEnd(const grpc_call_final_info* final_info) {
    interface_->RecordEnd(final_info);
  }

 private:
  ServerCallTracerInterface* interface_;
};

// Convenience functions to set call tracer on a call context. Allows setting
// multiple call tracers to a single call. It is only valid to add client call
// tracers before the client_channel filter sees the send_initial_metadata op.
void SetClientCallTracer(Arena* arena,
                         absl::Span<ClientCallTracerInterface* const> tracer);

// TODO(yashykt): We want server call tracers to be registered through the
// ServerCallTracerFactory, which has yet to be made into a list.
void SetServerCallTracer(Arena* arena,
                         absl::Span<ServerCallTracerInterface* const> tracer);

template <>
struct ArenaContextType<CallTracer> {
  static void Destroy(CallTracer*) {}
};

template <>
struct ArenaContextType<CallSpan> {
  static void Destroy(CallSpan*) {}
};

template <>
struct ContextSubclass<CallAttemptTracer> {
  using Base = CallTracer;
};

template <>
struct ContextSubclass<ServerCallTracer> {
  using Base = CallTracer;
};

template <>
struct ContextSubclass<ClientCallTracer> {
  using Base = CallSpan;
};

inline ClientCallTracer* WrapClientCallTracer(
    ClientCallTracerInterface* interface, Arena* arena) {
  if (interface == nullptr) return nullptr;
  return arena->ManagedNew<ClientCallTracer>(interface);
}

inline ServerCallTracer* WrapServerCallTracer(
    ServerCallTracerInterface* interface, Arena* arena) {
  if (interface == nullptr) return nullptr;
  return arena->ManagedNew<ServerCallTracer>(interface);
}

inline CallAttemptTracer* WrapCallAttemptTracer(
    ClientCallTracerInterface::CallAttemptTracer* interface, Arena* arena) {
  if (interface == nullptr) return nullptr;
  return arena->ManagedNew<CallAttemptTracer>(interface);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_CALL_TRACER_H
