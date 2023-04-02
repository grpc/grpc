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

#include <stdio.h>
#include <string.h>

#include <string>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/host_port.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

static void run_test(const char* target, size_t nops) {
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
  grpc_channel* channel;
  grpc_call* c;

  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_slice details;
  grpc_status_code status;
  grpc_call_error error;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_core::CqVerifier cqv(cq);

  grpc_op ops[6];
  grpc_op* op;

  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args args;

  args.num_args = 1;
  args.args = &ssl_name_override;

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  channel = grpc_channel_create(target, ssl_creds, &args);
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr:1234");
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               deadline, nullptr);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, nops, grpc_core::CqVerifier::tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Verify();

  GPR_ASSERT(status != GRPC_STATUS_OK);

  grpc_call_unref(c);
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_channel_credentials_release(ssl_creds);
}

int main(int argc, char** argv) {
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char* lunder = strrchr(me, '_');
  char* tmp;
  char root[1024];
  char test[64];
  int port = grpc_pick_unused_port_or_die();
  char* args[10];
  int status;
  size_t i;
  gpr_subprocess* svr;
  // figure out where we are
  if (lslash) {
    memcpy(root, me, static_cast<size_t>(lslash - me));
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }
  if (argc == 2) {
    grpc_core::SetEnv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", argv[1]);
  }
  // figure out our test name
  tmp = lunder - 1;
  while (*tmp != '_') tmp--;
  tmp++;
  memcpy(test, tmp, static_cast<size_t>(lunder - tmp));
  // start the server
  gpr_asprintf(&args[0], "%s/bad_ssl_%s_server%s", root, test,
               gpr_subprocess_binary_extension());
  args[1] = const_cast<char*>("--bind");
  std::string joined = grpc_core::JoinHostPort("::", port);
  args[2] = const_cast<char*>(joined.c_str());
  svr = gpr_subprocess_create(4, const_cast<const char**>(args));
  gpr_free(args[0]);

  for (i = 3; i <= 4; i++) {
    grpc_init();
    run_test(args[2], i);
    grpc_shutdown();
  }

  gpr_subprocess_interrupt(svr);
  status = gpr_subprocess_join(svr);
  gpr_subprocess_destroy(svr);
  return status;
}
