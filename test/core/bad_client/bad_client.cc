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

#include "test/core/bad_client/bad_client.h"

#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"

#define MIN_HTTP2_FRAME_SIZE 9

/* Args to provide to thread running server side validator */
typedef struct {
  grpc_server* server;
  grpc_completion_queue* cq;
  grpc_bad_client_server_side_validator validator;
  void* registered_method;
  gpr_event done_thd;
} thd_args;

/* Run the server side validator and set done_thd once done */
static void thd_func(void* arg) {
  thd_args* a = static_cast<thd_args*>(arg);
  if (a->validator != nullptr) {
    a->validator(a->server, a->cq, a->registered_method);
  }
  gpr_event_set(&a->done_thd, (void*)1);
}

/* Sets the done_write event */
static void set_done_write(void* arg, grpc_error* error) {
  gpr_event* done_write = static_cast<gpr_event*>(arg);
  gpr_event_set(done_write, (void*)1);
}

static void server_setup_transport(void* ts, grpc_transport* transport) {
  thd_args* a = static_cast<thd_args*>(ts);
  grpc_core::ExecCtx exec_ctx;
  grpc_server_setup_transport(a->server, transport, nullptr,
                              grpc_server_get_channel_args(a->server));
}

/* Sets the read_done event */
static void set_read_done(void* arg, grpc_error* error) {
  gpr_event* read_done = static_cast<gpr_event*>(arg);
  gpr_event_set(read_done, (void*)1);
}

/* shutdown client */
static void shutdown_client(grpc_endpoint** client_fd) {
  if (*client_fd != nullptr) {
    grpc_endpoint_shutdown(
        *client_fd, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Disconnect"));
    grpc_endpoint_destroy(*client_fd);
    grpc_core::ExecCtx::Get()->Flush();
    *client_fd = nullptr;
  }
}

/* Runs client side validator */
void grpc_run_client_side_validator(grpc_bad_client_arg* arg, uint32_t flags,
                                    grpc_endpoint_pair* sfd,
                                    grpc_completion_queue* client_cq) {
  char* hex;
  gpr_event done_write;
  if (arg->client_payload_length < 4 * 1024) {
    hex = gpr_dump(arg->client_payload, arg->client_payload_length,
                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
    /* Add a debug log */
    gpr_log(GPR_INFO, "TEST: %s", hex);
    gpr_free(hex);
  } else {
    gpr_log(GPR_INFO, "TEST: (%" PRIdPTR " byte long string)",
            arg->client_payload_length);
  }

  grpc_slice slice = grpc_slice_from_copied_buffer(arg->client_payload,
                                                   arg->client_payload_length);
  grpc_slice_buffer outgoing;
  grpc_closure done_write_closure;
  gpr_event_init(&done_write);

  grpc_slice_buffer_init(&outgoing);
  grpc_slice_buffer_add(&outgoing, slice);
  GRPC_CLOSURE_INIT(&done_write_closure, set_done_write, &done_write,
                    grpc_schedule_on_exec_ctx);

  /* Write data */
  grpc_endpoint_write(sfd->client, &outgoing, &done_write_closure);
  grpc_core::ExecCtx::Get()->Flush();

  /* Await completion, unless the request is large and write may not finish
   * before the peer shuts down. */
  if (!(flags & GRPC_BAD_CLIENT_LARGE_REQUEST)) {
    GPR_ASSERT(
        gpr_event_wait(&done_write, grpc_timeout_seconds_to_deadline(5)));
  }

  if (flags & GRPC_BAD_CLIENT_DISCONNECT) {
    shutdown_client(&sfd->client);
  }

  if (sfd->client != nullptr) {
    /* Validate client stream, if requested. */
    if (arg->client_validator != nullptr) {
      gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
      grpc_slice_buffer incoming;
      grpc_slice_buffer_init(&incoming);
      /* We may need to do multiple reads to read the complete server
       * response. */
      while (true) {
        gpr_event read_done_event;
        gpr_event_init(&read_done_event);
        grpc_closure read_done_closure;
        GRPC_CLOSURE_INIT(&read_done_closure, set_read_done, &read_done_event,
                          grpc_schedule_on_exec_ctx);
        grpc_endpoint_read(sfd->client, &incoming, &read_done_closure);
        grpc_core::ExecCtx::Get()->Flush();
        do {
          GPR_ASSERT(gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) > 0);
          /* Perform a cq next just to provide a thread that can read incoming
           bytes on the client fd */
          GPR_ASSERT(grpc_completion_queue_next(
                         client_cq, grpc_timeout_milliseconds_to_deadline(100),
                         nullptr)
                         .type == GRPC_QUEUE_TIMEOUT);
        } while (!gpr_event_get(&read_done_event));
        if (arg->client_validator(&incoming, arg->client_validator_arg)) break;
        gpr_log(GPR_INFO,
                "client validator failed; trying additional read "
                "in case we didn't get all the data");
      }
      grpc_slice_buffer_destroy_internal(&incoming);
    }
    grpc_core::ExecCtx::Get()->Flush();
  }

  /* If the request was too large, then we need to forcefully shut down the
   * client, so that the write can be considered completed */
  if (flags & GRPC_BAD_CLIENT_LARGE_REQUEST) {
    shutdown_client(&sfd->client);
  }

  /* Make sure that the client is done writing */
  while (!gpr_event_get(&done_write)) {
    GPR_ASSERT(
        grpc_completion_queue_next(
            client_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr)
            .type == GRPC_QUEUE_TIMEOUT);
  }

  grpc_slice_buffer_destroy_internal(&outgoing);
  grpc_core::ExecCtx::Get()->Flush();
}

void grpc_run_bad_client_test(
    grpc_bad_client_server_side_validator server_validator,
    grpc_bad_client_arg args[], int num_args, uint32_t flags) {
  grpc_endpoint_pair sfd;
  thd_args a;
  grpc_transport* transport;
  grpc_core::ExecCtx exec_ctx;
  grpc_completion_queue* shutdown_cq;
  grpc_completion_queue* client_cq;

  /* Init grpc */
  grpc_init();

  /* Create endpoints */
  sfd = grpc_iomgr_create_endpoint_pair("fixture", nullptr);

  /* Create server, completion events */
  a.server = grpc_server_create(nullptr, nullptr);
  a.cq = grpc_completion_queue_create_for_next(nullptr);
  client_cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_server_register_completion_queue(a.server, a.cq, nullptr);
  a.registered_method =
      grpc_server_register_method(a.server, GRPC_BAD_CLIENT_REGISTERED_METHOD,
                                  GRPC_BAD_CLIENT_REGISTERED_HOST,
                                  GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 0);
  grpc_server_start(a.server);
  transport = grpc_create_chttp2_transport(nullptr, sfd.server, false);
  server_setup_transport(&a, transport);
  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr);

  /* Bind fds to pollsets */
  grpc_endpoint_add_to_pollset(sfd.client, grpc_cq_pollset(client_cq));
  grpc_endpoint_add_to_pollset(sfd.server, grpc_cq_pollset(a.cq));

  /* Check a ground truth */
  GPR_ASSERT(grpc_server_has_open_connections(a.server));

  gpr_event_init(&a.done_thd);
  a.validator = server_validator;
  /* Start validator */

  grpc_core::Thread server_validator_thd("grpc_bad_client", thd_func, &a);
  server_validator_thd.Start();
  for (int i = 0; i < num_args; i++) {
    grpc_run_client_side_validator(&args[i], i == (num_args - 1) ? flags : 0,
                                   &sfd, client_cq);
  }
  /* Wait for server thread to finish */
  GPR_ASSERT(gpr_event_wait(&a.done_thd, grpc_timeout_seconds_to_deadline(1)));

  /* Shutdown. */
  shutdown_client(&sfd.client);
  server_validator_thd.Join();

  shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  grpc_server_shutdown_and_notify(a.server, shutdown_cq, nullptr);
  GPR_ASSERT(grpc_completion_queue_pluck(shutdown_cq, nullptr,
                                         grpc_timeout_seconds_to_deadline(1),
                                         nullptr)
                 .type == GRPC_OP_COMPLETE);
  grpc_completion_queue_destroy(shutdown_cq);
  grpc_server_destroy(a.server);
  grpc_completion_queue_destroy(a.cq);
  grpc_completion_queue_destroy(client_cq);
  grpc_shutdown();
}

bool client_connection_preface_validator(grpc_slice_buffer* incoming,
                                         void* arg) {
  if (incoming->count < 1) {
    return false;
  }
  grpc_slice slice = incoming->slices[0];
  /* There should be atleast a settings frame present */
  if (GRPC_SLICE_LENGTH(slice) < MIN_HTTP2_FRAME_SIZE) {
    return false;
  }
  const uint8_t* p = GRPC_SLICE_START_PTR(slice);
  /* Check the frame type (SETTINGS) */
  if (*(p + 3) != 4) {
    return false;
  }
  return true;
}

/* connection preface and settings frame to be sent by the client */
#define CONNECTION_PREFACE_FROM_CLIENT \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"   \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00"

grpc_bad_client_arg connection_preface_arg = {
    client_connection_preface_validator, nullptr,
    CONNECTION_PREFACE_FROM_CLIENT, sizeof(CONNECTION_PREFACE_FROM_CLIENT) - 1};

bool rst_stream_client_validator(grpc_slice_buffer* incoming, void* arg) {
  // Get last frame from incoming slice buffer.
  grpc_slice_buffer last_frame_buffer;
  grpc_slice_buffer_init(&last_frame_buffer);
  grpc_slice_buffer_trim_end(incoming, 13, &last_frame_buffer);
  GPR_ASSERT(last_frame_buffer.count == 1);
  grpc_slice last_frame = last_frame_buffer.slices[0];

  const uint8_t* p = GRPC_SLICE_START_PTR(last_frame);
  bool success =
      // Length == 4
      *p++ != 0 || *p++ != 0 || *p++ != 4 ||
      // Frame type (RST_STREAM)
      *p++ != 3 ||
      // Flags
      *p++ != 0 ||
      // Stream ID.
      *p++ != 0 || *p++ != 0 || *p++ != 0 || *p++ != 1 ||
      // Payload (error code)
      *p++ == 0 || *p++ == 0 || *p++ == 0 || *p == 0 || *p == 11;

  if (!success) {
    gpr_log(GPR_INFO, "client expected RST_STREAM frame, not found");
  }

  grpc_slice_buffer_destroy(&last_frame_buffer);
  return success;
}

static void* tag(intptr_t t) { return (void*)t; }

void server_verifier_request_call(grpc_server* server,
                                  grpc_completion_queue* cq,
                                  void* registered_method) {
  grpc_call_error error;
  grpc_call* s;
  grpc_call_details call_details;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;

  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.host, "localhost"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo/bar"));

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
}
