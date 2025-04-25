//
//
// Copyright 2023 gRPC authors.
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

#include "src/core/telemetry/call_tracer.h"

#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "src/core/lib/promise/context.h"
#include "src/core/telemetry/tcp_tracer.h"

namespace grpc_core {

CallTracerInterface::TransportByteSize&
CallTracerInterface::TransportByteSize::operator+=(
    const CallTracerInterface::TransportByteSize& other) {
  framing_bytes += other.framing_bytes;
  data_bytes += other.data_bytes;
  header_bytes += other.header_bytes;
  return *this;
}

//
// ServerCallTracerFactory
//

namespace {

ServerCallTracerFactory* g_server_call_tracer_factory_ = nullptr;

const char* kServerCallTracerFactoryChannelArgName =
    "grpc.experimental.server_call_tracer_factory";

}  // namespace

ServerCallTracerFactory* ServerCallTracerFactory::Get(
    const ChannelArgs& channel_args) {
  ServerCallTracerFactory* factory =
      channel_args.GetObject<ServerCallTracerFactory>();
  if (factory == nullptr) {
    factory = g_server_call_tracer_factory_;
  }
  if (factory && factory->IsServerTraced(channel_args)) {
    return factory;
  }
  return nullptr;
}

void ServerCallTracerFactory::RegisterGlobal(ServerCallTracerFactory* factory) {
  g_server_call_tracer_factory_ = factory;
}

void ServerCallTracerFactory::TestOnlyReset() {
  delete g_server_call_tracer_factory_;
  g_server_call_tracer_factory_ = nullptr;
}

absl::string_view ServerCallTracerFactory::ChannelArgName() {
  return kServerCallTracerFactoryChannelArgName;
}

class DelegatingClientCallTracer : public ClientCallTracer {
 public:
  class DelegatingClientCallAttemptTracer
      : public ClientCallTracer::CallAttemptTracer {
   public:
    explicit DelegatingClientCallAttemptTracer(
        std::vector<CallAttemptTracer*> tracers)
        : tracers_(std::move(tracers)) {
      DCHECK(!tracers_.empty());
    }
    ~DelegatingClientCallAttemptTracer() override {}
    void RecordSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata) override {
      for (auto* tracer : tracers_) {
        tracer->RecordSendInitialMetadata(send_initial_metadata);
      }
    }
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* send_trailing_metadata) override {
      for (auto* tracer : tracers_) {
        tracer->RecordSendTrailingMetadata(send_trailing_metadata);
      }
    }
    void RecordSendMessage(const Message& send_message) override {
      for (auto* tracer : tracers_) {
        tracer->RecordSendMessage(send_message);
      }
    }
    void RecordSendCompressedMessage(
        const Message& send_compressed_message) override {
      for (auto* tracer : tracers_) {
        tracer->RecordSendCompressedMessage(send_compressed_message);
      }
    }
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* recv_initial_metadata) override {
      for (auto* tracer : tracers_) {
        tracer->RecordReceivedInitialMetadata(recv_initial_metadata);
      }
    }
    void RecordReceivedMessage(const Message& recv_message) override {
      for (auto* tracer : tracers_) {
        tracer->RecordReceivedMessage(recv_message);
      }
    }
    void RecordReceivedDecompressedMessage(
        const Message& recv_decompressed_message) override {
      for (auto* tracer : tracers_) {
        tracer->RecordReceivedDecompressedMessage(recv_decompressed_message);
      }
    }
    void RecordCancel(grpc_error_handle cancel_error) override {
      for (auto* tracer : tracers_) {
        tracer->RecordCancel(cancel_error);
      }
    }
    void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) override {
      for (auto* tracer : tracers_) {
        tracer->RecordReceivedTrailingMetadata(status, recv_trailing_metadata,
                                               transport_stream_stats);
      }
    }
    void RecordEnd() override {
      for (auto* tracer : tracers_) {
        tracer->RecordEnd();
      }
    }
    void RecordIncomingBytes(
        const TransportByteSize& transport_byte_size) override {
      for (auto* tracer : tracers_) {
        tracer->RecordIncomingBytes(transport_byte_size);
      }
    }
    void RecordOutgoingBytes(
        const TransportByteSize& transport_byte_size) override {
      for (auto* tracer : tracers_) {
        tracer->RecordOutgoingBytes(transport_byte_size);
      }
    }
    void RecordAnnotation(absl::string_view annotation) override {
      for (auto* tracer : tracers_) {
        tracer->RecordAnnotation(annotation);
      }
    }
    void RecordAnnotation(const Annotation& annotation) override {
      for (auto* tracer : tracers_) {
        tracer->RecordAnnotation(annotation);
      }
    }
    std::shared_ptr<TcpCallTracer> StartNewTcpTrace() override {
      return nullptr;
    }
    void SetOptionalLabel(OptionalLabelKey key,
                          RefCountedStringValue value) override {
      for (auto* tracer : tracers_) {
        tracer->SetOptionalLabel(key, value);
      }
    }
    std::string TraceId() override { return tracers_[0]->TraceId(); }
    std::string SpanId() override { return tracers_[0]->SpanId(); }
    bool IsSampled() override { return tracers_[0]->IsSampled(); }
    bool IsDelegatingTracer() override { return true; }

   private:
    // There is no additional synchronization needed since filters/interceptors
    // will be adding call tracers to the context and these are already
    // synchronized through promises/call combiners (single promise running per
    // call at any moment).
    std::vector<CallAttemptTracer*> tracers_;
  };
  explicit DelegatingClientCallTracer(ClientCallTracer* tracer)
      : tracers_{tracer} {}
  ~DelegatingClientCallTracer() override {}
  CallAttemptTracer* StartNewAttempt(bool is_transparent_retry) override {
    std::vector<CallAttemptTracer*> attempt_tracers;
    attempt_tracers.reserve(tracers_.size());
    for (auto* tracer : tracers_) {
      auto* attempt_tracer = tracer->StartNewAttempt(is_transparent_retry);
      DCHECK_NE(attempt_tracer, nullptr);
      attempt_tracers.push_back(attempt_tracer);
    }
    return GetContext<Arena>()->ManagedNew<DelegatingClientCallAttemptTracer>(
        std::move(attempt_tracers));
  }

  void RecordAnnotation(absl::string_view annotation) override {
    for (auto* tracer : tracers_) {
      tracer->RecordAnnotation(annotation);
    }
  }
  void RecordAnnotation(const Annotation& annotation) override {
    for (auto* tracer : tracers_) {
      tracer->RecordAnnotation(annotation);
    }
  }
  std::string TraceId() override { return tracers_[0]->TraceId(); }
  std::string SpanId() override { return tracers_[0]->SpanId(); }
  bool IsSampled() override { return tracers_[0]->IsSampled(); }
  bool IsDelegatingTracer() override { return true; }

  // There is no additional synchronization needed since filters/interceptors
  // will be adding call tracers to the context and these are already
  // synchronized through promises/call combiners (single promise running per
  // call at any moment).
  void AddTracer(ClientCallTracer* tracer) { tracers_.push_back(tracer); }

 private:
  std::vector<ClientCallTracer*> tracers_;
};

class DelegatingServerCallTracer : public ServerCallTracer {
 public:
  explicit DelegatingServerCallTracer(ServerCallTracer* tracer)
      : tracers_{tracer} {}
  ~DelegatingServerCallTracer() override {}
  void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) override {
    for (auto* tracer : tracers_) {
      tracer->RecordSendInitialMetadata(send_initial_metadata);
    }
  }
  void RecordSendTrailingMetadata(
      grpc_metadata_batch* send_trailing_metadata) override {
    for (auto* tracer : tracers_) {
      tracer->RecordSendTrailingMetadata(send_trailing_metadata);
    }
  }
  void RecordSendMessage(const Message& send_message) override {
    for (auto* tracer : tracers_) {
      tracer->RecordSendMessage(send_message);
    }
  }
  void RecordSendCompressedMessage(
      const Message& send_compressed_message) override {
    for (auto* tracer : tracers_) {
      tracer->RecordSendCompressedMessage(send_compressed_message);
    }
  }
  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata) override {
    for (auto* tracer : tracers_) {
      tracer->RecordReceivedInitialMetadata(recv_initial_metadata);
    }
  }
  void RecordReceivedMessage(const Message& recv_message) override {
    for (auto* tracer : tracers_) {
      tracer->RecordReceivedMessage(recv_message);
    }
  }
  void RecordReceivedDecompressedMessage(
      const Message& recv_decompressed_message) override {
    for (auto* tracer : tracers_) {
      tracer->RecordReceivedDecompressedMessage(recv_decompressed_message);
    }
  }
  void RecordCancel(grpc_error_handle cancel_error) override {
    for (auto* tracer : tracers_) {
      tracer->RecordCancel(cancel_error);
    }
  }
  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* recv_trailing_metadata) override {
    for (auto* tracer : tracers_) {
      tracer->RecordReceivedTrailingMetadata(recv_trailing_metadata);
    }
  }
  void RecordEnd(const grpc_call_final_info* final_info) override {
    for (auto* tracer : tracers_) {
      tracer->RecordEnd(final_info);
    }
  }
  void RecordIncomingBytes(
      const TransportByteSize& transport_byte_size) override {
    for (auto* tracer : tracers_) {
      tracer->RecordIncomingBytes(transport_byte_size);
    }
  }
  void RecordOutgoingBytes(
      const TransportByteSize& transport_byte_size) override {
    for (auto* tracer : tracers_) {
      tracer->RecordOutgoingBytes(transport_byte_size);
    }
  }
  void RecordAnnotation(absl::string_view annotation) override {
    for (auto* tracer : tracers_) {
      tracer->RecordAnnotation(annotation);
    }
  }
  void RecordAnnotation(const Annotation& annotation) override {
    for (auto* tracer : tracers_) {
      tracer->RecordAnnotation(annotation);
    }
  }
  std::shared_ptr<TcpCallTracer> StartNewTcpTrace() override { return nullptr; }
  std::string TraceId() override { return tracers_[0]->TraceId(); }
  std::string SpanId() override { return tracers_[0]->SpanId(); }
  bool IsSampled() override { return tracers_[0]->IsSampled(); }
  bool IsDelegatingTracer() override { return true; }

  void AddTracer(ServerCallTracer* tracer) { tracers_.push_back(tracer); }

 private:
  // The ServerCallTracerFilter will be responsible for making sure that the
  // tracers are added in a thread-safe manner. It is imagined that the filter
  // will just invoke the factories in the server call tracer factory list
  // sequentially, removing the need for any synchronization.
  std::vector<ServerCallTracer*> tracers_;
};

void AddClientCallTracerToContext(Arena* arena, ClientCallTracer* tracer) {
  if (arena->GetContext<CallTracerAnnotationInterface>() == nullptr) {
    // This is the first call tracer. Set it directly.
    arena->SetContext<CallTracerAnnotationInterface>(tracer);
  } else {
    // There was already a call tracer present.
    auto* orig_tracer = DownCast<ClientCallTracer*>(
        arena->GetContext<CallTracerAnnotationInterface>());
    if (orig_tracer->IsDelegatingTracer()) {
      // We already created a delegating tracer. Just add the new tracer to the
      // list.
      DownCast<DelegatingClientCallTracer*>(orig_tracer)->AddTracer(tracer);
    } else {
      // Create a new delegating tracer and add the first tracer and the new
      // tracer to the list.
      auto* delegating_tracer =
          GetContext<Arena>()->ManagedNew<DelegatingClientCallTracer>(
              orig_tracer);
      arena->SetContext<CallTracerAnnotationInterface>(delegating_tracer);
      delegating_tracer->AddTracer(tracer);
    }
  }
}

void AddServerCallTracerToContext(Arena* arena, ServerCallTracer* tracer) {
  DCHECK_EQ(arena->GetContext<CallTracerInterface>(),
            arena->GetContext<CallTracerAnnotationInterface>());
  if (arena->GetContext<CallTracerAnnotationInterface>() == nullptr) {
    // This is the first call tracer. Set it directly.
    arena->SetContext<CallTracerAnnotationInterface>(tracer);
    arena->SetContext<CallTracerInterface>(tracer);
  } else {
    // There was already a call tracer present.
    auto* orig_tracer = DownCast<ServerCallTracer*>(
        arena->GetContext<CallTracerAnnotationInterface>());
    if (orig_tracer->IsDelegatingTracer()) {
      // We already created a delegating tracer. Just add the new tracer to the
      // list.
      DownCast<DelegatingServerCallTracer*>(orig_tracer)->AddTracer(tracer);
    } else {
      // Create a new delegating tracer and add the first tracer and the new
      // tracer to the list.
      auto* delegating_tracer =
          GetContext<Arena>()->ManagedNew<DelegatingServerCallTracer>(
              orig_tracer);
      arena->SetContext<CallTracerAnnotationInterface>(delegating_tracer);
      arena->SetContext<CallTracerInterface>(delegating_tracer);
      delegating_tracer->AddTracer(tracer);
    }
  }
}

}  // namespace grpc_core
