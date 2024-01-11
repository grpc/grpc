//
//
// Copyright 2015 gRPC authors.
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

#include <string.h>

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

class Watcher : public grpc_core::ConnectivityStateWatcherInterface {
 public:
  void Notify(grpc_connectivity_state new_state,
              const absl::Status& /* status */) override {
    ASSERT_EQ(new_state, GRPC_CHANNEL_SHUTDOWN);
  }
};

static grpc_closure transport_op_cb;

static void do_nothing(void* /*arg*/, grpc_error_handle /*error*/) {}

void test_transport_op(grpc_channel* channel) {
  grpc_core::ExecCtx exec_ctx;
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->start_connectivity_watch = grpc_core::MakeOrphanable<Watcher>();
  grpc_channel_element* elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  elem->filter->start_transport_op(elem, op);

  GRPC_CLOSURE_INIT(&transport_op_cb, do_nothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  op = grpc_make_transport_op(&transport_op_cb);
  elem->filter->start_transport_op(elem, op);
}

TEST(LameClientTest, MainTest) {
  grpc_channel* chan;
  grpc_call* call;
  grpc_completion_queue* cq;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  char* peer;

  grpc_init();

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  const char* error_message = "Rpc sent on a lame channel.";
  grpc_status_code error_code = GRPC_STATUS_ABORTED;
  chan = grpc_lame_client_channel_create("lampoon:national", error_code,
                                         error_message);
  ASSERT_TRUE(chan);

  test_transport_op(chan);

  ASSERT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE,
            grpc_channel_check_connectivity_state(chan, 0));

  cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_slice host = grpc_slice_from_static_string("anywhere");
  call =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/Foo"), &host,
                               grpc_timeout_seconds_to_deadline(100), nullptr);
  ASSERT_TRUE(call);
  grpc_core::CqVerifier cqv(cq);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  ASSERT_EQ(GRPC_CALL_OK, error);

  // Filter stack code considers this a failed to receive initial metadata
  // result, where as promise based code interprets this as a trailers only
  // failed request. Both are rational interpretations, so we accept the one
  // that is implemented for each stack.
  cqv.Expect(grpc_core::CqVerifier::tag(1),
             grpc_core::IsPromiseBasedClientCallEnabled());
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(2), nullptr);
  ASSERT_EQ(GRPC_CALL_OK, error);

  // the call should immediately fail
  cqv.Expect(grpc_core::CqVerifier::tag(2), true);
  cqv.Verify();

  peer = grpc_call_get_peer(call);
  ASSERT_STREQ(peer, "lampoon:national");
  gpr_free(peer);

  ASSERT_EQ(status, error_code);
  ASSERT_EQ(grpc_slice_str_cmp(details, error_message), 0);

  grpc_call_unref(call);
  grpc_channel_destroy(chan);
  grpc_completion_queue_destroy(cq);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);

  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
