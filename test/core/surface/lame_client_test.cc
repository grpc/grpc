/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

class Watcher : public grpc_core::ConnectivityStateWatcherInterface {
 public:
  void Notify(grpc_connectivity_state new_state,
              const absl::Status& /* status */) override {
    GPR_ASSERT(new_state == GRPC_CHANNEL_SHUTDOWN);
  }
};

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

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

int main(int argc, char** argv) {
  grpc_channel* chan;
  grpc_call* call;
  grpc_completion_queue* cq;
  cq_verifier* cqv;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  char* peer;

  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  const char* error_message = "Rpc sent on a lame channel.";
  grpc_status_code error_code = GRPC_STATUS_ABORTED;
  chan = grpc_lame_client_channel_create("lampoon:national", error_code,
                                         error_message);
  GPR_ASSERT(chan);

  test_transport_op(chan);

  GPR_ASSERT(GRPC_CHANNEL_SHUTDOWN ==
             grpc_channel_check_connectivity_state(chan, 0));

  cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_slice host = grpc_slice_from_static_string("anywhere");
  call =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/Foo"), &host,
                               grpc_timeout_seconds_to_deadline(100), nullptr);
  GPR_ASSERT(call);
  cqv = cq_verifier_create(cq);

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
                                tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  /* the call should immediately fail */
  CQ_EXPECT_COMPLETION(cqv, tag(1), 0);
  cq_verify(cqv);

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
                                tag(2), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  /* the call should immediately fail */
  CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
  cq_verify(cqv);

  peer = grpc_call_get_peer(call);
  GPR_ASSERT(strcmp(peer, "lampoon:national") == 0);
  gpr_free(peer);

  GPR_ASSERT(status == error_code);
  GPR_ASSERT(grpc_slice_str_cmp(details, error_message) == 0);

  grpc_call_unref(call);
  grpc_channel_destroy(chan);
  cq_verifier_destroy(cqv);
  grpc_completion_queue_destroy(cq);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);

  grpc_shutdown();

  return 0;
}
