/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/ext/client_channel/initial_connect_string.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_tcp_server.h"

struct rpc_state {
  char *target;
  grpc_channel_credentials *creds;
  grpc_completion_queue *cq;
  grpc_channel *channel;
  grpc_call *call;
  grpc_op op;
  grpc_slice_buffer incoming_buffer;
  grpc_slice_buffer temp_incoming_buffer;
  grpc_endpoint *tcp;
  gpr_atm done_atm;
};

static const char *magic_connect_string = "magic initial string";
static int server_port;
static struct rpc_state state;
static grpc_closure on_read;

static void handle_read(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_slice_buffer_move_into(&state.temp_incoming_buffer,
                              &state.incoming_buffer);
  gpr_log(GPR_DEBUG, "got %" PRIuPTR " bytes, magic is %" PRIuPTR " bytes",
          state.incoming_buffer.length, strlen(magic_connect_string));
  if (state.incoming_buffer.length > strlen(magic_connect_string)) {
    gpr_atm_rel_store(&state.done_atm, 1);
    grpc_endpoint_shutdown(exec_ctx, state.tcp);
    grpc_endpoint_destroy(exec_ctx, state.tcp);
  } else {
    grpc_endpoint_read(exec_ctx, state.tcp, &state.temp_incoming_buffer,
                       &on_read);
  }
}

static void on_connect(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *tcp,
                       grpc_pollset *accepting_pollset,
                       grpc_tcp_server_acceptor *acceptor) {
  gpr_free(acceptor);
  test_tcp_server *server = arg;
  grpc_closure_init(&on_read, handle_read, NULL, grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&state.incoming_buffer);
  grpc_slice_buffer_init(&state.temp_incoming_buffer);
  state.tcp = tcp;
  grpc_endpoint_add_to_pollset(exec_ctx, tcp, server->pollset);
  grpc_endpoint_read(exec_ctx, tcp, &state.temp_incoming_buffer, &on_read);
}

static void set_magic_initial_string(grpc_resolved_address **addr,
                                     grpc_slice *connect_string) {
  GPR_ASSERT(addr);
  GPR_ASSERT((*addr)->len);
  *connect_string = grpc_slice_from_copied_string(magic_connect_string);
}

static void reset_addr_and_set_magic_string(grpc_resolved_address **addr,
                                            grpc_slice *connect_string) {
  struct sockaddr_in target;
  *connect_string = grpc_slice_from_copied_string(magic_connect_string);
  gpr_free(*addr);
  target.sin_family = AF_INET;
  target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  target.sin_port = htons((uint16_t)server_port);
  *addr = (grpc_resolved_address *)gpr_malloc(sizeof(grpc_resolved_address));
  (*addr)->len = sizeof(target);
  memcpy((*addr)->addr, &target, sizeof(target));
}

static gpr_timespec n_sec_deadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

static void start_rpc(int use_creds, int target_port) {
  state.cq = grpc_completion_queue_create(NULL);
  if (use_creds) {
    state.creds = grpc_fake_transport_security_credentials_create();
  } else {
    state.creds = NULL;
  }
  gpr_join_host_port(&state.target, "127.0.0.1", target_port);
  if (use_creds) {
    state.channel =
        grpc_secure_channel_create(state.creds, state.target, NULL, NULL);
  } else {
    state.channel = grpc_insecure_channel_create(state.target, NULL, NULL);
  }
  state.call = grpc_channel_create_call(
      state.channel, NULL, GRPC_PROPAGATE_DEFAULTS, state.cq, "/Service/Method",
      "localhost", gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  memset(&state.op, 0, sizeof(state.op));
  state.op.op = GRPC_OP_SEND_INITIAL_METADATA;
  state.op.data.send_initial_metadata.count = 0;
  state.op.flags = 0;
  state.op.reserved = NULL;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(state.call, &state.op,
                                                   (size_t)(1), NULL, NULL));
  grpc_completion_queue_next(state.cq, n_sec_deadline(5), NULL);
}

static void cleanup_rpc(void) {
  grpc_event ev;
  grpc_slice_buffer_destroy(&state.incoming_buffer);
  grpc_slice_buffer_destroy(&state.temp_incoming_buffer);
  grpc_channel_credentials_release(state.creds);
  grpc_call_destroy(state.call);
  grpc_completion_queue_shutdown(state.cq);
  do {
    ev = grpc_completion_queue_next(state.cq, n_sec_deadline(1), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(state.cq);
  grpc_channel_destroy(state.channel);
  gpr_free(state.target);
}

typedef struct {
  test_tcp_server *server;
  gpr_event *signal_when_done;
} poll_args;

static void actually_poll_server(void *arg) {
  poll_args *pa = arg;
  gpr_timespec deadline = n_sec_deadline(10);
  while (true) {
    bool done = gpr_atm_acq_load(&state.done_atm) != 0;
    gpr_timespec time_left =
        gpr_time_sub(deadline, gpr_now(GPR_CLOCK_REALTIME));
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64 ".%09" PRId32, done,
            time_left.tv_sec, time_left.tv_nsec);
    if (done || gpr_time_cmp(time_left, gpr_time_0(GPR_TIMESPAN)) < 0) {
      break;
    }
    test_tcp_server_poll(pa->server, 1);
  }
  gpr_event_set(pa->signal_when_done, (void *)1);
  gpr_free(pa);
}

static void poll_server_until_read_done(test_tcp_server *server,
                                        gpr_event *signal_when_done) {
  gpr_atm_rel_store(&state.done_atm, 0);
  gpr_thd_id id;
  poll_args *pa = gpr_malloc(sizeof(*pa));
  pa->server = server;
  pa->signal_when_done = signal_when_done;
  gpr_thd_new(&id, actually_poll_server, pa, NULL);
}

static void match_initial_magic_string(grpc_slice_buffer *buffer) {
  size_t i, j, cmp_length;
  size_t magic_length = strlen(magic_connect_string);
  GPR_ASSERT(buffer->length >= magic_length);
  for (i = 0, j = 0; i < state.incoming_buffer.count && j < magic_length; i++) {
    char *dump =
        grpc_dump_slice(state.incoming_buffer.slices[i], GPR_DUMP_ASCII);
    cmp_length = GPR_MIN(strlen(dump), magic_length - j);
    GPR_ASSERT(strncmp(dump, magic_connect_string + j, cmp_length) == 0);
    j += cmp_length;
    gpr_free(dump);
  }
}

static void test_initial_string(test_tcp_server *server, int secure) {
  gpr_event ev;
  gpr_event_init(&ev);
  grpc_test_set_initial_connect_string_function(set_magic_initial_string);
  poll_server_until_read_done(server, &ev);
  start_rpc(secure, server_port);
  gpr_event_wait(&ev, gpr_inf_future(GPR_CLOCK_REALTIME));
  match_initial_magic_string(&state.incoming_buffer);
  cleanup_rpc();
}

static void test_initial_string_with_redirect(test_tcp_server *server,
                                              int secure) {
  gpr_event ev;
  gpr_event_init(&ev);
  int another_port = grpc_pick_unused_port_or_die();
  grpc_test_set_initial_connect_string_function(
      reset_addr_and_set_magic_string);
  poll_server_until_read_done(server, &ev);
  start_rpc(secure, another_port);
  gpr_event_wait(&ev, gpr_inf_future(GPR_CLOCK_REALTIME));
  match_initial_magic_string(&state.incoming_buffer);
  cleanup_rpc();
}

static void run_test(void (*test)(test_tcp_server *server, int secure),
                     int secure) {
  test_tcp_server test_server;
  server_port = grpc_pick_unused_port_or_die();
  test_tcp_server_init(&test_server, on_connect, &test_server);
  test_tcp_server_start(&test_server, server_port);
  test(&test_server, secure);
  test_tcp_server_destroy(&test_server);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  run_test(test_initial_string, 0);
  run_test(test_initial_string, 1);
  run_test(test_initial_string_with_redirect, 0);
  run_test(test_initial_string_with_redirect, 1);

  grpc_shutdown();
  return 0;
}
