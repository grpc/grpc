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
#include "src/core/lib/gprpp/examine_stack.h"

namespace grpc_core {

class FakeClientCallTracer : public ClientCallTracer {
 public:
  class FakeClientCallAttemptTracer
      : public ClientCallTracer::CallAttemptTracer {
   public:
    explicit FakeClientCallAttemptTracer(
        std::vector<std::string>* annotation_logger)
        : annotation_logger_(annotation_logger) {}
    ~FakeClientCallAttemptTracer() override {
      std::cout << "~FakeClientCallAttemptTracer: " << this << "\n";
      absl::optional<std::string> stacktrace =
          grpc_core::GetCurrentStackTrace();
      if (stacktrace.has_value()) {
        gpr_log(GPR_DEBUG, "%s", stacktrace->c_str());
      } else {
        gpr_log(GPR_DEBUG, "stacktrace unavailable");
      }
    }
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
    void AddOptionalLabels(OptionalLabelComponent component,
                           std::shared_ptr<std::map<std::string, std::string>>
                               service_labels) override {
      optional_labels_.emplace(component, *service_labels);
      for (const auto& pair : optional_labels_) {
        for (const auto& ipair : pair.second) {
          std::cout << ipair.first << ", " << ipair.second << "\n";
        }
      }
    }
    std::string TraceId() override { return ""; }
    std::string SpanId() override { return ""; }
    bool IsSampled() override { return false; }

    std::map<OptionalLabelComponent, std::map<std::string, std::string>>
    GetoptionalLabels() const {
      return optional_labels_;
    }

   private:
    std::vector<std::string>* annotation_logger_;
    std::map<OptionalLabelComponent, std::map<std::string, std::string>>
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

void InjectGlobalFakeClientCallTracer(
    FakeClientCallTracer* fake_client_call_tracer);

class FakeStatsClientFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<FakeStatsClientFilter> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;
};

void RegisterFakeStatsPlugin();

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
