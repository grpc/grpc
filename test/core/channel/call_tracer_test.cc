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

#include "src/core/lib/channel/call_tracer.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

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
    void RecordEnd(const gpr_timespec& /*latency*/) override { delete this; }
    void RecordAnnotation(absl::string_view annotation) override {
      annotation_logger_->push_back(std::string(annotation));
    }
    void RecordAnnotation(const Annotation& /*annotation*/) override {}
    std::string TraceId() override { return ""; }
    std::string SpanId() override { return ""; }
    bool IsSampled() override { return false; }

   private:
    std::vector<std::string>* annotation_logger_;
  };

  explicit FakeClientCallTracer(std::vector<std::string>* annotation_logger)
      : annotation_logger_(annotation_logger) {}
  ~FakeClientCallTracer() override {}
  CallAttemptTracer* StartNewAttempt(bool /*is_transparent_retry*/) override {
    return new FakeClientCallAttemptTracer(annotation_logger_);
  }

  void RecordAnnotation(absl::string_view annotation) override {
    annotation_logger_->push_back(std::string(annotation));
  }
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  std::vector<std::string>* annotation_logger_;
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
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  std::vector<std::string>* annotation_logger_;
};

class CallTracerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    memory_allocator_ = new MemoryAllocator(
        ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
            "test"));
    arena_ = Arena::Create(1024, memory_allocator_);
  }

  void TearDown() override {
    arena_->Destroy();
    delete memory_allocator_;
  }

  MemoryAllocator* memory_allocator_ = nullptr;
  Arena* arena_ = nullptr;
  grpc_call_context_element context_[GRPC_CONTEXT_COUNT] = {};
  std::vector<std::string> annotation_logger_;
};

TEST_F(CallTracerTest, BasicClientCallTracer) {
  FakeClientCallTracer client_call_tracer(&annotation_logger_);
  AddClientCallTracerToContext(context_, &client_call_tracer);
  static_cast<CallTracerAnnotationInterface*>(
      context_[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value)
      ->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_, std::vector<std::string>{"Test"});
}

TEST_F(CallTracerTest, MultipleClientCallTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_);
  FakeClientCallTracer client_call_tracer1(&annotation_logger_);
  FakeClientCallTracer client_call_tracer2(&annotation_logger_);
  FakeClientCallTracer client_call_tracer3(&annotation_logger_);
  AddClientCallTracerToContext(context_, &client_call_tracer1);
  AddClientCallTracerToContext(context_, &client_call_tracer2);
  AddClientCallTracerToContext(context_, &client_call_tracer3);
  static_cast<CallTracerAnnotationInterface*>(
      context_[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value)
      ->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
}

TEST_F(CallTracerTest, MultipleClientCallAttemptTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_);
  FakeClientCallTracer client_call_tracer1(&annotation_logger_);
  FakeClientCallTracer client_call_tracer2(&annotation_logger_);
  FakeClientCallTracer client_call_tracer3(&annotation_logger_);
  AddClientCallTracerToContext(context_, &client_call_tracer1);
  AddClientCallTracerToContext(context_, &client_call_tracer2);
  AddClientCallTracerToContext(context_, &client_call_tracer3);
  auto* attempt_tracer =
      static_cast<ClientCallTracer*>(
          context_[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value)
          ->StartNewAttempt(true /* is_transparent_retry */);
  attempt_tracer->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
}

TEST_F(CallTracerTest, BasicServerCallTracerTest) {
  FakeServerCallTracer server_call_tracer(&annotation_logger_);
  AddServerCallTracerToContext(context_, &server_call_tracer);
  static_cast<CallTracerAnnotationInterface*>(
      context_[GRPC_CONTEXT_CALL_TRACER].value)
      ->RecordAnnotation("Test");
  static_cast<CallTracerAnnotationInterface*>(
      context_[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value)
      ->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_, std::vector<std::string>({"Test", "Test"}));
}

TEST_F(CallTracerTest, MultipleServerCallTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_);
  FakeServerCallTracer server_call_tracer1(&annotation_logger_);
  FakeServerCallTracer server_call_tracer2(&annotation_logger_);
  FakeServerCallTracer server_call_tracer3(&annotation_logger_);
  AddServerCallTracerToContext(context_, &server_call_tracer1);
  AddServerCallTracerToContext(context_, &server_call_tracer2);
  AddServerCallTracerToContext(context_, &server_call_tracer3);
  static_cast<CallTracerAnnotationInterface*>(
      context_[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value)
      ->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
