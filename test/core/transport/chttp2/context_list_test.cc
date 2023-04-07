//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/context_list.h"

#include <stdint.h>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/atm.h>

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

const uint32_t kByteOffset = 123;

void* PhonyArgsCopier(void* arg) { return arg; }

void TestExecuteFlushesListVerifier(void* arg, Timestamps* ts,
                                    grpc_error_handle error) {
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(error, absl::OkStatus());
  if (ts) {
    EXPECT_EQ(ts->byte_offset, kByteOffset);
  }
  gpr_atm* done = reinterpret_cast<gpr_atm*>(arg);
  gpr_atm_rel_store(done, gpr_atm{1});
}

class ContextListTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_http2_set_write_timestamps_callback(TestExecuteFlushesListVerifier);
    grpc_http2_set_fn_get_copied_context(PhonyArgsCopier);
  }
};

/// Tests that all ContextList elements in the list are flushed out on
/// execute.
/// Also tests that arg and byte_counter are passed correctly.
///
TEST_F(ContextListTest, ExecuteFlushesList) {
  ContextList* list = ContextList::MakeNewContextList();
  const int kNumElems = 5;
  gpr_atm verifier_called[kNumElems];
  for (auto i = 0; i < kNumElems; i++) {
    gpr_atm_rel_store(&verifier_called[i], gpr_atm{0});
    list->Append(&verifier_called[i], kByteOffset, i * kByteOffset,
                 kByteOffset);
  }
  Timestamps ts;
  ContextList::Execute(list, &ts, absl::OkStatus());
  for (auto i = 0; i < kNumElems; i++) {
    EXPECT_EQ(gpr_atm_acq_load(&verifier_called[i]), 1);
  }
}

TEST_F(ContextListTest, EmptyList) {
  ContextList* list = ContextList::MakeNewContextList();
  Timestamps ts;
  ContextList::Execute(list, &ts, absl::OkStatus());
}

TEST_F(ContextListTest, EmptyListEmptyTimestamp) {
  ContextList* list = ContextList::MakeNewContextList();
  ContextList::Execute(list, nullptr, absl::OkStatus());
}

TEST_F(ContextListTest, NonEmptyListEmptyTimestamp) {
  ContextList* list = ContextList::MakeNewContextList();
  const int kNumElems = 5;
  gpr_atm verifier_called[kNumElems];
  for (auto i = 0; i < kNumElems; i++) {
    gpr_atm_rel_store(&verifier_called[i], gpr_atm{0});
    list->Append(&verifier_called[i], kByteOffset, i * kByteOffset,
                 kByteOffset);
  }
  ContextList::Execute(list, nullptr, absl::OkStatus());
  for (auto i = 0; i < kNumElems; i++) {
    EXPECT_EQ(gpr_atm_acq_load(&verifier_called[i]), 1);
  }
}

TEST_F(ContextListTest, IterateAndFreeTest) {
  ContextList* list = ContextList::MakeNewContextList();
  const int kNumElems = 50;
  int verifier_context[kNumElems];
  for (auto i = 0; i < kNumElems; i++) {
    verifier_context[i] = i;
    list->Append(&verifier_context[i], kByteOffset, i * kByteOffset,
                 kByteOffset);
  }
  int i = 0;
  ContextList::ForEachExecuteCallback(
      list,
      [&i](void* trace_context, size_t byte_offset,
           int64_t traced_bytes_relative_start_pos, int64_t num_traced_bytes) {
        int* verifier_context = static_cast<int*>(trace_context);
        // It should iterate in the forward order
        EXPECT_EQ(*verifier_context, i);
        EXPECT_EQ(byte_offset, kByteOffset);
        EXPECT_EQ(traced_bytes_relative_start_pos,
                  static_cast<int64_t>(i * kByteOffset));
        EXPECT_EQ(num_traced_bytes, static_cast<int64_t>(kByteOffset));
        i++;
      });
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
