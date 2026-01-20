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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_string.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

std::string SendInitialMetadataAnnotation::ToString() const {
  return "SendInitialMetadata";
}

void SendInitialMetadataAnnotation::ForEachKeyValue(
    absl::FunctionRef<void(absl::string_view, ValueType)> f) const {
  metadata_->Log([f](absl::string_view key, absl::string_view value) {
    if (IsMetadataKeyAllowedInDebugOutput(key)) {
      f(key, value);
    } else {
      f(key, "[REDACTED]");
    }
  });
}

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

class DelegatingClientCallTracer : public ClientCallTracerInterface {
 public:
  class DelegatingClientCallAttemptTracer
      : public ClientCallTracerInterface::CallAttemptTracer {
   public:
    explicit DelegatingClientCallAttemptTracer(
        std::vector<CallAttemptTracer*> tracers)
        : tracers_(std::move(tracers)) {
      GRPC_DCHECK(!tracers_.empty());
    }
    ~DelegatingClientCallAttemptTracer() override {}
    void RecordSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata) override {
      for (auto* tracer : tracers_) {
        tracer->RecordSendInitialMetadata(send_initial_metadata);
      }
    }
    void MutateSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata) override {
      for (auto* tracer : tracers_) {
        tracer->MutateSendInitialMetadata(send_initial_metadata);
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
  explicit DelegatingClientCallTracer(ClientCallTracerInterface* tracer)
      : tracers_{tracer} {}
  explicit DelegatingClientCallTracer(
      absl::Span<ClientCallTracerInterface* const> tracers)
      : tracers_(tracers.begin(), tracers.end()) {}
  ~DelegatingClientCallTracer() override {}
  CallAttemptTracer* StartNewAttempt(bool is_transparent_retry) override {
    std::vector<CallAttemptTracer*> attempt_tracers;
    attempt_tracers.reserve(tracers_.size());
    for (auto* tracer : tracers_) {
      auto* attempt_tracer = tracer->StartNewAttempt(is_transparent_retry);
      GRPC_DCHECK_NE(attempt_tracer, nullptr);
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
  void AddTracer(ClientCallTracerInterface* tracer) {
    tracers_.push_back(tracer);
  }

 private:
  std::vector<ClientCallTracerInterface*> tracers_;
};

class DelegatingServerCallTracer : public ServerCallTracerInterface {
 public:
  explicit DelegatingServerCallTracer(ServerCallTracerInterface* tracer)
      : tracers_{tracer} {}
  explicit DelegatingServerCallTracer(
      absl::Span<ServerCallTracerInterface* const> tracers)
      : tracers_(tracers.begin(), tracers.end()) {}
  ~DelegatingServerCallTracer() override {}
  void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) override {
    for (auto* tracer : tracers_) {
      tracer->RecordSendInitialMetadata(send_initial_metadata);
    }
  }
  void MutateSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) override {
    for (auto* tracer : tracers_) {
      tracer->MutateSendInitialMetadata(send_initial_metadata);
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

  void AddTracer(ServerCallTracerInterface* tracer) {
    tracers_.push_back(tracer);
  }

 private:
  // The ServerCallTracerFilter will be responsible for making sure that the
  // tracers are added in a thread-safe manner. It is imagined that the filter
  // will just invoke the factories in the server call tracer factory list
  // sequentially, removing the need for any synchronization.
  std::vector<ServerCallTracerInterface*> tracers_;
};

void CallTracer::RecordSendInitialMetadata(
    grpc_metadata_batch* send_initial_metadata) {
  if (IsCallTracerSendInitialMetadataIsAnAnnotationEnabled()) {
    RecordAnnotation(SendInitialMetadataAnnotation(send_initial_metadata));
    interface_->MutateSendInitialMetadata(send_initial_metadata);
  } else {
    interface_->RecordSendInitialMetadata(send_initial_metadata);
  }
}

void SetClientCallTracer(Arena* arena,
                         absl::Span<ClientCallTracerInterface* const> tracer) {
  GRPC_DCHECK_EQ(arena->GetContext<CallSpan>(), nullptr);
  switch (tracer.size()) {
    case 0:
      return;
    case 1:
      arena->SetContext<CallSpan>(WrapClientCallTracer(tracer[0], arena));
      return;
    default:
      auto* delegating_tracer =
          GetContext<Arena>()->ManagedNew<DelegatingClientCallTracer>(tracer);
      arena->SetContext<CallSpan>(
          WrapClientCallTracer(delegating_tracer, arena));
      break;
  }
}

void SetServerCallTracer(Arena* arena,
                         absl::Span<ServerCallTracerInterface* const> tracer) {
  GRPC_DCHECK_EQ(arena->GetContext<CallSpan>(), nullptr);
  switch (tracer.size()) {
    case 0:
      return;
    case 1:
      arena->SetContext<CallSpan>(WrapServerCallTracer(tracer[0], arena));
      arena->SetContext<CallTracer>(WrapServerCallTracer(tracer[0], arena));
      return;
    default:
      auto* delegating_tracer =
          GetContext<Arena>()->ManagedNew<DelegatingServerCallTracer>(tracer);
      auto* wrapper = WrapServerCallTracer(delegating_tracer, arena);
      arena->SetContext<CallSpan>(wrapper);
      arena->SetContext<CallTracer>(wrapper);
      break;
  }
}

}  // namespace grpc_core