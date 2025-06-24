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

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include <vector>

#include "gtest/gtest.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

class CallTracerTest : public ::testing::Test {
 protected:
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  std::vector<std::string> annotation_logger_;
};

TEST_F(CallTracerTest, BasicClientCallTracer) {
  FakeClientCallTracer client_call_tracer(&annotation_logger_);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer);
  arena_->GetContext<CallTracerAnnotationInterface>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_, std::vector<std::string>{"Test"});
}

TEST_F(CallTracerTest, MultipleClientCallTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  FakeClientCallTracer client_call_tracer1(&annotation_logger_);
  FakeClientCallTracer client_call_tracer2(&annotation_logger_);
  FakeClientCallTracer client_call_tracer3(&annotation_logger_);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer1);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer2);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer3);
  arena_->GetContext<CallTracerAnnotationInterface>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
}

TEST_F(CallTracerTest, MultipleClientCallAttemptTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  FakeClientCallTracer client_call_tracer1(&annotation_logger_);
  FakeClientCallTracer client_call_tracer2(&annotation_logger_);
  FakeClientCallTracer client_call_tracer3(&annotation_logger_);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer1);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer2);
  AddClientCallTracerToContext(arena_.get(), &client_call_tracer3);
  auto* attempt_tracer =
      DownCast<ClientCallTracer*>(
          arena_->GetContext<CallTracerAnnotationInterface>())
          ->StartNewAttempt(true /* is_transparent_retry */);
  attempt_tracer->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_,
            std::vector<std::string>({"Test", "Test", "Test"}));
  attempt_tracer->RecordEnd();
}

TEST_F(CallTracerTest, BasicServerCallTracerTest) {
  FakeServerCallTracer server_call_tracer(&annotation_logger_);
  AddServerCallTracerToContext(arena_.get(), &server_call_tracer);
  arena_->GetContext<CallTracerAnnotationInterface>()->RecordAnnotation("Test");
  arena_->GetContext<CallTracerAnnotationInterface>()->RecordAnnotation("Test");
  EXPECT_EQ(annotation_logger_, std::vector<std::string>({"Test", "Test"}));
}

TEST_F(CallTracerTest, MultipleServerCallTracers) {
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  FakeServerCallTracer server_call_tracer1(&annotation_logger_);
  FakeServerCallTracer server_call_tracer2(&annotation_logger_);
  FakeServerCallTracer server_call_tracer3(&annotation_logger_);
  AddServerCallTracerToContext(arena_.get(), &server_call_tracer1);
  AddServerCallTracerToContext(arena_.get(), &server_call_tracer2);
  AddServerCallTracerToContext(arena_.get(), &server_call_tracer3);
  arena_->GetContext<CallTracerAnnotationInterface>()->RecordAnnotation("Test");
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
