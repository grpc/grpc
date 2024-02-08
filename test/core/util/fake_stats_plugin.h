// Copyright 2023 The gRPC Authors.
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

#ifndef GRPC_TEST_CORE_UTIL_FAKE_STATS_PLUGIN_H
#define GRPC_TEST_CORE_UTIL_FAKE_STATS_PLUGIN_H

#include <memory>
#include <string>
#include <vector>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/channel/tcp_tracer.h"

namespace grpc_core {

// Registers a FakeStatsClientFilter as a client channel filter if there is a
// FakeClientCallTracerFactory in the channel args. This filter will use the
// FakeClientCallTracerFactory to create and inject a FakeClientCallTracer into
// the call context.
// Example usage:
//   RegisterFakeStatsPlugin();  // before grpc_init()
//
//   // Creates a FakeClientCallTracerFactory and adds it into the channel args.
//   FakeClientCallTracerFactory fake_client_call_tracer_factory;
//   ChannelArguments channel_args;
//   channel_args.SetPointer(GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY,
//                           &fake_client_call_tracer_factory);
//
//   // After the system under test has been executed (e.g. an RPC has been
//   // sent), use the FakeClientCallTracerFactory to verify certain
//   // expectations.
//   EXPECT_THAT(fake_client_call_tracer_factory.GetLastFakeClientCallTracer()
//                   ->GetLastCallAttemptTracer()
//                   ->GetOptionalLabels(),
//               VerifyCsmServiceLabels());
void RegisterFakeStatsPlugin();

class FakeClientCallTracer : public ClientCallTracer {
 public:
  class FakeClientCallAttemptTracer
      : public ClientCallTracer::CallAttemptTracer {
   public:
    explicit FakeClientCallAttemptTracer(
        std::vector<std::string>* annotation_logger)
        : annotation_logger_(annotation_logger) {}
    ~FakeClientCallAttemptTracer() override {}
    void RecordSendInitialMetadata(
        grpc_metadata_batch* /*send_initial_metadata*/) override {}
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /*send_trailing_metadata*/) override {}
    void RecordSendMessage(const SliceBuffer& /*send_message*/) override {}
    void RecordSendCompressedMessage(
        const SliceBuffer& /*send_compressed_message*/) override {}
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /*recv_initial_metadata*/) override {}
    void RecordReceivedMessage(const SliceBuffer& /*recv_message*/) override {}
    void RecordReceivedDecompressedMessage(
        const SliceBuffer& /*recv_decompressed_message*/) override {}
    void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
    void RecordReceivedTrailingMetadata(
        absl::Status /*status*/,
        grpc_metadata_batch* /*recv_trailing_metadata*/,
        const grpc_transport_stream_stats* /*transport_stream_stats*/)
        override {}
    void RecordEnd(const gpr_timespec& /*latency*/) override {}
    void RecordAnnotation(absl::string_view annotation) override {
      annotation_logger_->push_back(std::string(annotation));
    }
    void RecordAnnotation(const Annotation& /*annotation*/) override {}
    std::shared_ptr<TcpTracerInterface> StartNewTcpTrace() override {
      return nullptr;
    }
    void AddOptionalLabels(
        OptionalLabelComponent component,
        std::shared_ptr<std::map<std::string, std::string>> labels) override {
      optional_labels_.emplace(component, std::move(labels));
    }
    std::string TraceId() override { return ""; }
    std::string SpanId() override { return ""; }
    bool IsSampled() override { return false; }

    const std::map<OptionalLabelComponent,
                   std::shared_ptr<std::map<std::string, std::string>>>&
    GetOptionalLabels() const {
      return optional_labels_;
    }

   private:
    std::vector<std::string>* annotation_logger_;
    std::map<OptionalLabelComponent,
             std::shared_ptr<std::map<std::string, std::string>>>
        optional_labels_;
  };

  explicit FakeClientCallTracer(std::vector<std::string>* annotation_logger)
      : annotation_logger_(annotation_logger) {}
  ~FakeClientCallTracer() override {}
  CallAttemptTracer* StartNewAttempt(bool /*is_transparent_retry*/) override {
    call_attempt_tracers_.emplace_back(
        new FakeClientCallAttemptTracer(annotation_logger_));
    return call_attempt_tracers_.back().get();
  }

  void RecordAnnotation(absl::string_view annotation) override {
    annotation_logger_->push_back(std::string(annotation));
  }
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

  FakeClientCallAttemptTracer* GetLastCallAttemptTracer() const {
    return call_attempt_tracers_.back().get();
  }

 private:
  std::vector<std::string>* annotation_logger_;
  std::vector<std::unique_ptr<FakeClientCallAttemptTracer>>
      call_attempt_tracers_;
};

#define GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY \
  "grpc.testing.inject_fake_client_call_tracer_factory"

class FakeClientCallTracerFactory {
 public:
  FakeClientCallTracer* CreateFakeClientCallTracer() {
    fake_client_call_tracers_.emplace_back(
        new FakeClientCallTracer(&annotation_logger_));
    return fake_client_call_tracers_.back().get();
  }

  FakeClientCallTracer* GetLastFakeClientCallTracer() {
    return fake_client_call_tracers_.back().get();
  }

 private:
  std::vector<std::string> annotation_logger_;
  std::vector<std::unique_ptr<FakeClientCallTracer>> fake_client_call_tracers_;
};

class FakeServerCallTracer : public ServerCallTracer {
 public:
  explicit FakeServerCallTracer(std::vector<std::string>* annotation_logger)
      : annotation_logger_(annotation_logger) {}
  ~FakeServerCallTracer() override {}
  void RecordSendInitialMetadata(
      grpc_metadata_batch* /*send_initial_metadata*/) override {}
  void RecordSendTrailingMetadata(
      grpc_metadata_batch* /*send_trailing_metadata*/) override {}
  void RecordSendMessage(const SliceBuffer& /*send_message*/) override {}
  void RecordSendCompressedMessage(
      const SliceBuffer& /*send_compressed_message*/) override {}
  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* /*recv_initial_metadata*/) override {}
  void RecordReceivedMessage(const SliceBuffer& /*recv_message*/) override {}
  void RecordReceivedDecompressedMessage(
      const SliceBuffer& /*recv_decompressed_message*/) override {}
  void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* /*recv_trailing_metadata*/) override {}
  void RecordEnd(const grpc_call_final_info* /*final_info*/) override {}
  void RecordAnnotation(absl::string_view annotation) override {
    annotation_logger_->push_back(std::string(annotation));
  }
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::shared_ptr<TcpTracerInterface> StartNewTcpTrace() override {
    return nullptr;
  }
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  std::vector<std::string>* annotation_logger_;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_FAKE_STATS_PLUGIN_H
