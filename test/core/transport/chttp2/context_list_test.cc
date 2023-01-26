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

#include <algorithm>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "test/core/util/mock_endpoint.h"
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

void discard_write(grpc_slice /*slice*/) {}

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
  ContextList* list = nullptr;
  const int kNumElems = 5;
  ExecCtx exec_ctx;
  grpc_stream_refcount ref;
  GRPC_STREAM_REF_INIT(&ref, 1, nullptr, nullptr, "phony ref");
  grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(discard_write);
  auto args = CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  grpc_transport* t = grpc_create_chttp2_transport(args, mock_endpoint, true);
  std::vector<grpc_chttp2_stream*> s;
  s.reserve(kNumElems);
  gpr_atm verifier_called[kNumElems];
  for (auto i = 0; i < kNumElems; i++) {
    s.push_back(static_cast<grpc_chttp2_stream*>(
        gpr_malloc(grpc_transport_stream_size(t))));
    grpc_transport_init_stream(reinterpret_cast<grpc_transport*>(t),
                               reinterpret_cast<grpc_stream*>(s[i]), &ref,
                               nullptr, nullptr);
    s[i]->context = &verifier_called[i];
    s[i]->byte_counter = kByteOffset;
    gpr_atm_rel_store(&verifier_called[i], gpr_atm{0});
    ContextList::Append(&list, s[i]);
  }
  Timestamps ts;
  ContextList::Execute(list, &ts, absl::OkStatus());
  for (auto i = 0; i < kNumElems; i++) {
    EXPECT_EQ(gpr_atm_acq_load(&verifier_called[i]), 1);
    grpc_transport_destroy_stream(reinterpret_cast<grpc_transport*>(t),
                                  reinterpret_cast<grpc_stream*>(s[i]),
                                  nullptr);
    exec_ctx.Flush();
    gpr_free(s[i]);
  }
  grpc_transport_destroy(t);
  exec_ctx.Flush();
}

TEST_F(ContextListTest, EmptyList) {
  ContextList* list = nullptr;
  ExecCtx exec_ctx;
  Timestamps ts;
  ContextList::Execute(list, &ts, absl::OkStatus());
  exec_ctx.Flush();
}

TEST_F(ContextListTest, EmptyListEmptyTimestamp) {
  ContextList* list = nullptr;
  ExecCtx exec_ctx;
  ContextList::Execute(list, nullptr, absl::OkStatus());
  exec_ctx.Flush();
}

TEST_F(ContextListTest, NonEmptyListEmptyTimestamp) {
  ContextList* list = nullptr;
  const int kNumElems = 5;
  ExecCtx exec_ctx;
  grpc_stream_refcount ref;
  GRPC_STREAM_REF_INIT(&ref, 1, nullptr, nullptr, "phony ref");
  grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(discard_write);
  auto args = CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  grpc_transport* t = grpc_create_chttp2_transport(args, mock_endpoint, true);
  std::vector<grpc_chttp2_stream*> s;
  s.reserve(kNumElems);
  gpr_atm verifier_called[kNumElems];
  for (auto i = 0; i < kNumElems; i++) {
    s.push_back(static_cast<grpc_chttp2_stream*>(
        gpr_malloc(grpc_transport_stream_size(t))));
    grpc_transport_init_stream(reinterpret_cast<grpc_transport*>(t),
                               reinterpret_cast<grpc_stream*>(s[i]), &ref,
                               nullptr, nullptr);
    s[i]->context = &verifier_called[i];
    s[i]->byte_counter = kByteOffset;
    gpr_atm_rel_store(&verifier_called[i], gpr_atm{0});
    ContextList::Append(&list, s[i]);
  }
  ContextList::Execute(list, nullptr, absl::OkStatus());
  for (auto i = 0; i < kNumElems; i++) {
    EXPECT_EQ(gpr_atm_acq_load(&verifier_called[i]), 1);
    grpc_transport_destroy_stream(reinterpret_cast<grpc_transport*>(t),
                                  reinterpret_cast<grpc_stream*>(s[i]),
                                  nullptr);
    exec_ctx.Flush();
    gpr_free(s[i]);
  }
  grpc_transport_destroy(t);
  exec_ctx.Flush();
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
