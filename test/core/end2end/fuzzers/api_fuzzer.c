/*
 *
 * Copyright 2016, Google Inc.
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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/metadata.h"
#include "test/core/util/passthru_endpoint.h"

////////////////////////////////////////////////////////////////////////////////
// logging

static const bool squelch = true;

static void dont_log(gpr_log_func_args *args) {}

////////////////////////////////////////////////////////////////////////////////
// input_stream: allows easy access to input bytes, and allows reading a little
//               past the end (avoiding needing to check everywhere)

typedef struct {
  const uint8_t *cur;
  const uint8_t *end;
} input_stream;

static uint8_t next_byte(input_stream *inp) {
  if (inp->cur == inp->end) {
    return 0;
  }
  return *inp->cur++;
}

static void end(input_stream *inp) { inp->cur = inp->end; }

static char *read_string(input_stream *inp) {
  size_t len = next_byte(inp);
  char *str = gpr_malloc(len + 1);
  for (size_t i = 0; i < len; i++) {
    str[i] = (char)next_byte(inp);
  }
  str[len] = 0;
  return str;
}

static void read_buffer(input_stream *inp, char **buffer, size_t *length) {
  *length = next_byte(inp);
  *buffer = gpr_malloc(*length);
  for (size_t i = 0; i < *length; i++) {
    (*buffer)[i] = (char)next_byte(inp);
  }
}

static uint32_t read_uint32(input_stream *inp) {
  uint8_t b = next_byte(inp);
  uint32_t x = b & 0x7f;
  if (b & 0x80) {
    x <<= 7;
    b = next_byte(inp);
    x |= b & 0x7f;
    if (b & 0x80) {
      x <<= 7;
      b = next_byte(inp);
      x |= b & 0x7f;
      if (b & 0x80) {
        x <<= 7;
        b = next_byte(inp);
        x |= b & 0x7f;
        if (b & 0x80) {
          x = (x << 4) | (next_byte(inp) & 0x0f);
        }
      }
    }
  }
  return x;
}

static grpc_byte_buffer *read_message(input_stream *inp) {
  gpr_slice slice = gpr_slice_malloc(read_uint32(inp));
  memset(GPR_SLICE_START_PTR(slice), 0, GPR_SLICE_LENGTH(slice));
  return grpc_raw_byte_buffer_create(&slice, 1);
}

static void read_metadata(input_stream *inp, size_t *count, grpc_metadata **metadata) {
  *count = next_byte(inp);
  *metadata = gpr_malloc(*count * sizeof(**metadata));
  memset(*metadata, 0, *count * sizeof(**metadata));
  for (size_t i = 0; i < *count; i++) {
    (*metadata)[i].key = read_string(inp);
    read_buffer(inp, (char**)&(*metadata[i]).value, &(*metadata[i]).value_length);
    (*metadata)[i].flags = read_uint32(inp);
  }
}

static int read_int(input_stream *inp) { return (int)read_uint32(inp); }

static grpc_channel_args *read_args(input_stream *inp) {
  size_t n = next_byte(inp);
  grpc_arg *args = gpr_malloc(sizeof(*args) * n);
  for (size_t i = 0; i < n; i++) {
    bool is_string = next_byte(inp) & 1;
    args[i].type = is_string ? GRPC_ARG_STRING : GRPC_ARG_INTEGER;
    args[i].key = read_string(inp);
    if (is_string) {
      args[i].value.string = read_string(inp);
    } else {
      args[i].value.integer = read_int(inp);
    }
  }
  grpc_channel_args *a = gpr_malloc(sizeof(*a));
  a->args = args;
  a->num_args = n;
  return a;
}

static bool is_eof(input_stream *inp) { return inp->cur == inp->end; }

////////////////////////////////////////////////////////////////////////////////
// global state

static gpr_timespec g_now;
static grpc_server *g_server;
static grpc_channel *g_channel;

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  return g_now;
}

////////////////////////////////////////////////////////////////////////////////
// dns resolution

typedef struct addr_req {
  grpc_timer timer;
  char *addr;
  grpc_resolve_cb cb;
  void *arg;
} addr_req;

static void finish_resolve(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  addr_req *r = arg;

  if (success && 0 == strcmp(r->addr, "server")) {
    grpc_resolved_addresses *addrs = gpr_malloc(sizeof(*addrs));
    addrs->naddrs = 1;
    addrs->addrs = gpr_malloc(sizeof(*addrs->addrs));
    addrs->addrs[0].len = 0;
    r->cb(exec_ctx, r->arg, addrs);
  } else {
    r->cb(exec_ctx, r->arg, NULL);
  }

  gpr_free(r->addr);
  gpr_free(r);
}

void my_resolve_address(grpc_exec_ctx *exec_ctx, const char *addr,
                        const char *default_port, grpc_resolve_cb cb,
                        void *arg) {
  addr_req *r = gpr_malloc(sizeof(*r));
  r->addr = gpr_strdup(addr);
  r->cb = cb;
  r->arg = arg;
  grpc_timer_init(exec_ctx, &r->timer,
                  gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)),
                  finish_resolve, r, gpr_now(GPR_CLOCK_MONOTONIC));
}

////////////////////////////////////////////////////////////////////////////////
// client connection

// defined in tcp_client_posix.c
extern void (*grpc_tcp_client_connect_impl)(
    grpc_exec_ctx *exec_ctx, grpc_closure *closure, grpc_endpoint **ep,
    grpc_pollset_set *interested_parties, const struct sockaddr *addr,
    size_t addr_len, gpr_timespec deadline);

static void sched_connect(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                          grpc_endpoint **ep, gpr_timespec deadline);

typedef struct {
  grpc_timer timer;
  grpc_closure *closure;
  grpc_endpoint **ep;
  gpr_timespec deadline;
} future_connect;

static void do_connect(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  future_connect *fc = arg;
  if (!success) {
    *fc->ep = NULL;
    grpc_exec_ctx_enqueue(exec_ctx, fc->closure, false, NULL);
  } else if (g_server != NULL) {
    grpc_endpoint *client;
    grpc_endpoint *server;
    grpc_passthru_endpoint_create(&client, &server);
    *fc->ep = client;

    grpc_transport *transport =
        grpc_create_chttp2_transport(exec_ctx, NULL, server, 0);
    grpc_server_setup_transport(exec_ctx, g_server, transport, NULL);
    grpc_chttp2_transport_start_reading(exec_ctx, transport, NULL, 0);

    grpc_exec_ctx_enqueue(exec_ctx, fc->closure, false, NULL);
  } else {
    sched_connect(exec_ctx, fc->closure, fc->ep, fc->deadline);
  }
  gpr_free(fc);
}

static void sched_connect(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                          grpc_endpoint **ep, gpr_timespec deadline) {
  if (gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) <= 0) {
    *ep = NULL;
    grpc_exec_ctx_enqueue(exec_ctx, closure, false, NULL);
    return;
  }

  future_connect *fc = gpr_malloc(sizeof(*fc));
  fc->closure = closure;
  fc->ep = ep;
  fc->deadline = deadline;
  grpc_timer_init(exec_ctx, &fc->timer,
                  gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_millis(1, GPR_TIMESPAN)),
                  do_connect, fc, gpr_now(GPR_CLOCK_MONOTONIC));
}

static void my_tcp_client_connect(grpc_exec_ctx *exec_ctx,
                                  grpc_closure *closure, grpc_endpoint **ep,
                                  grpc_pollset_set *interested_parties,
                                  const struct sockaddr *addr, size_t addr_len,
                                  gpr_timespec deadline) {
  sched_connect(exec_ctx, closure, ep, deadline);
}

////////////////////////////////////////////////////////////////////////////////
// test driver

typedef struct validator {
  void (*validate)(void *arg, bool success);
  void *arg;
} validator;

static validator *create_validator(void (*validate)(void *arg, bool success),
                                   void *arg) {
  validator *v = gpr_malloc(sizeof(*v));
  v->validate = validate;
  v->arg = arg;
  return v;
}

static void assert_success_and_decrement(void *counter, bool success) {
  GPR_ASSERT(success);
  --*(int *)counter;
}

static void decrement(void *counter, bool success) { --*(int *)counter; }

typedef struct connectivity_watch {
  int *counter;
  gpr_timespec deadline;
} connectivity_watch;

static connectivity_watch *make_connectivity_watch(gpr_timespec s,
                                                   int *counter) {
  connectivity_watch *o = gpr_malloc(sizeof(*o));
  o->deadline = s;
  o->counter = counter;
  return o;
}

static void validate_connectivity_watch(void *p, bool success) {
  connectivity_watch *w = p;
  if (!success) {
    GPR_ASSERT(gpr_time_cmp(gpr_now(w->deadline.clock_type), w->deadline) >= 0);
  }
  --*w->counter;
  gpr_free(w);
}

static void free_non_null(void *p) {
  GPR_ASSERT(p != NULL);
  gpr_free(p);
}

typedef struct call_state {
  grpc_call *client;
  grpc_call *server;
  grpc_byte_buffer *recv_message[2];
  grpc_status_code status;
  grpc_metadata_array recv_initial_metadata;
  grpc_metadata_array recv_trailing_metadata;
  char *recv_status_details;
  size_t recv_status_details_capacity;
  int cancelled;
} call_state;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  grpc_test_only_set_metadata_hash_seed(0);
  if (squelch) gpr_set_log_function(dont_log);
  input_stream inp = {data, data + size};
  grpc_resolve_address = my_resolve_address;
  grpc_tcp_client_connect_impl = my_tcp_client_connect;
  gpr_now_impl = now_impl;
  grpc_init();

  GPR_ASSERT(g_channel == NULL);
  GPR_ASSERT(g_server == NULL);

  bool server_shutdown = false;
  int pending_server_shutdowns = 0;
  int pending_channel_watches = 0;
  int pending_pings = 0;
  int pending_ops = 0;

#define MAX_CALLS 16
  call_state calls[MAX_CALLS];
  int num_calls = 0;
  memset(calls, 0, sizeof(calls));

  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);

  while (!is_eof(&inp) || g_channel != NULL || g_server != NULL ||
         pending_channel_watches > 0 || pending_pings > 0 || pending_ops > 0) {
    if (is_eof(&inp)) {
      if (g_channel != NULL) {
        grpc_channel_destroy(g_channel);
        g_channel = NULL;
      }
      if (g_server != NULL) {
        if (!server_shutdown) {
          grpc_server_shutdown_and_notify(
              g_server, cq, create_validator(assert_success_and_decrement,
                                             &pending_server_shutdowns));
          server_shutdown = true;
          pending_server_shutdowns++;
        } else if (pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = NULL;
        }
      }

      g_now = gpr_time_add(g_now, gpr_time_from_seconds(1, GPR_TIMESPAN));
    }

    switch (next_byte(&inp)) {
      // terminate on bad bytes
      default:
        end(&inp);
        break;
      // tickle completion queue
      case 0: {
        grpc_event ev = grpc_completion_queue_next(
            cq, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
        switch (ev.type) {
          case GRPC_OP_COMPLETE: {
            validator *v = ev.tag;
            v->validate(v->arg, ev.success);
            gpr_free(v);
            break;
          }
          case GRPC_QUEUE_TIMEOUT:
            break;
          case GRPC_QUEUE_SHUTDOWN:
            abort();
            break;
        }
        break;
      }
      // increment global time
      case 1: {
        g_now = gpr_time_add(
            g_now, gpr_time_from_micros(read_uint32(&inp), GPR_TIMESPAN));
        break;
      }
      // create an insecure channel
      case 2: {
        if (g_channel == NULL) {
          char *target = read_string(&inp);
          char *target_uri;
          gpr_asprintf(&target_uri, "dns:%s", target);
          grpc_channel_args *args = read_args(&inp);
          g_channel = grpc_insecure_channel_create(target_uri, args, NULL);
          GPR_ASSERT(g_channel != NULL);
          grpc_channel_args_destroy(args);
          gpr_free(target_uri);
          gpr_free(target);
        } else {
          end(&inp);
        }
        break;
      }
      // destroy a channel
      case 3: {
        if (g_channel != NULL) {
          grpc_channel_destroy(g_channel);
          g_channel = NULL;
        } else {
          end(&inp);
        }
        break;
      }
      // bring up a server
      case 4: {
        if (g_server == NULL) {
          grpc_channel_args *args = read_args(&inp);
          g_server = grpc_server_create(args, NULL);
          GPR_ASSERT(g_server != NULL);
          grpc_channel_args_destroy(args);
          grpc_server_register_completion_queue(g_server, cq, NULL);
          grpc_server_start(g_server);
          server_shutdown = false;
          GPR_ASSERT(pending_server_shutdowns == 0);
        } else {
          end(&inp);
        }
      }
      // begin server shutdown
      case 5: {
        if (g_server != NULL) {
          grpc_server_shutdown_and_notify(
              g_server, cq, create_validator(assert_success_and_decrement,
                                             &pending_server_shutdowns));
          pending_server_shutdowns++;
          server_shutdown = true;
        } else {
          end(&inp);
        }
        break;
      }
      // cancel all calls if shutdown
      case 6: {
        if (g_server != NULL && server_shutdown) {
          grpc_server_cancel_all_calls(g_server);
        } else {
          end(&inp);
        }
        break;
      }
      // destroy server
      case 7: {
        if (g_server != NULL && server_shutdown &&
            pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = NULL;
        } else {
          end(&inp);
        }
        break;
      }
      // check connectivity
      case 8: {
        if (g_channel != NULL) {
          uint8_t try_to_connect = next_byte(&inp);
          if (try_to_connect == 0 || try_to_connect == 1) {
            grpc_channel_check_connectivity_state(g_channel, try_to_connect);
          } else {
            end(&inp);
          }
        } else {
          end(&inp);
        }
        break;
      }
      // watch connectivity
      case 9: {
        if (g_channel != NULL) {
          grpc_connectivity_state st =
              grpc_channel_check_connectivity_state(g_channel, 0);
          if (st != GRPC_CHANNEL_FATAL_FAILURE) {
            gpr_timespec deadline = gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(read_uint32(&inp), GPR_TIMESPAN));
            grpc_channel_watch_connectivity_state(
                g_channel, st, deadline, cq,
                create_validator(validate_connectivity_watch,
                                 make_connectivity_watch(
                                     deadline, &pending_channel_watches)));
            pending_channel_watches++;
          }
        } else {
          end(&inp);
        }
        break;
      }
      // create a call
      case 10: {
        bool ok = true;
        if (g_channel == NULL) ok = false;
        if (num_calls >= MAX_CALLS) ok = false;
        grpc_call *parent_call = NULL;
        uint8_t pcidx = next_byte(&inp);
        if (pcidx > MAX_CALLS)
          ok = false;
        else if (pcidx < MAX_CALLS) {
          parent_call = calls[pcidx].server;
          if (parent_call == NULL) ok = false;
        }
        uint32_t propagation_mask = read_uint32(&inp);
        char *method = read_string(&inp);
        char *host = read_string(&inp);
        gpr_timespec deadline =
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(read_uint32(&inp), GPR_TIMESPAN));

        if (ok) {
          GPR_ASSERT(calls[num_calls].client == NULL);
          calls[num_calls].client =
              grpc_channel_create_call(g_channel, parent_call, propagation_mask,
                                       cq, method, host, deadline, NULL);
        } else {
          end(&inp);
        }
        break;
      }
      // switch the 'current' call
      case 11: {
        uint8_t new_current = next_byte(&inp);
        if (new_current == 0 || new_current >= num_calls) {
          end(&inp);
        } else {
          GPR_SWAP(call_state, calls[0], calls[new_current]);
        }
        break;
      }
      // queue some ops on a call
      case 12: {
        size_t num_ops = next_byte(&inp);
        grpc_op *ops = gpr_malloc(sizeof(grpc_op) * num_ops);
        bool ok = num_calls > 0;
        uint8_t on_server = next_byte(&inp);
        if (on_server != 0 && on_server != 1) {
          ok = false;
        }
        if (ok && on_server && calls[0].server == NULL) {
          ok = false;
        }
        if (ok && !on_server && calls[0].client == NULL) {
          ok = false;
        }
        size_t i;
        grpc_op *op;
        for (i = 0; i < num_ops; i++) {
          op = &ops[i];
          switch (next_byte(&inp)) {
            default:
              ok = false;
              break;
            case GRPC_OP_SEND_INITIAL_METADATA:
              op->op = GRPC_OP_SEND_INITIAL_METADATA;
              read_metadata(&inp, &op->data.send_initial_metadata.count,
                            &op->data.send_initial_metadata.metadata);
              break;
            case GRPC_OP_SEND_MESSAGE:
              op->op = GRPC_OP_SEND_INITIAL_METADATA;
              op->data.send_message = read_message(&inp);
              break;
            case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
              op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
              break;
            case GRPC_OP_SEND_STATUS_FROM_SERVER:
              op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
              read_metadata(
                  &inp,
                  &op->data.send_status_from_server.trailing_metadata_count,
                  &op->data.send_status_from_server.trailing_metadata);
              op->data.send_status_from_server.status = next_byte(&inp);
              op->data.send_status_from_server.status_details = read_string(&inp);
              break;
            case GRPC_OP_RECV_INITIAL_METADATA:
              op->op = GRPC_OP_RECV_INITIAL_METADATA;
              op->data.recv_initial_metadata = &calls[0].recv_initial_metadata;
              break;
            case GRPC_OP_RECV_MESSAGE:
              op->op = GRPC_OP_RECV_MESSAGE;
              op->data.recv_message = &calls[0].recv_message[on_server];
              break;
            case GRPC_OP_RECV_STATUS_ON_CLIENT:
              op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
              op->data.recv_status_on_client.status = &calls[0].status;
              op->data.recv_status_on_client.trailing_metadata =
                  &calls[0].recv_trailing_metadata;
              op->data.recv_status_on_client.status_details =
                  &calls[0].recv_status_details;
              op->data.recv_status_on_client.status_details_capacity =
                  &calls[0].recv_status_details_capacity;
              break;
            case GRPC_OP_RECV_CLOSE_ON_SERVER:
              op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
              op->data.recv_close_on_server.cancelled = &calls[0].cancelled;
              break;
          }
          op->reserved = NULL;
          op->flags = read_uint32(&inp);
          if (ok) {
            validator *v = create_validator(decrement, &pending_ops);
            pending_ops++;
            grpc_call_error error = grpc_call_start_batch(
                on_server ? calls[0].server : calls[0].client, ops, num_ops,
                v, NULL);
            if (error != GRPC_CALL_OK) {
              v->validate(v->arg, false);
              gpr_free(v);
            }
          } else {
            end(&inp);
          }
          for (i = 0; i < num_ops; i++) {
            op = &ops[i];
            switch (op->op) {
            case GRPC_OP_SEND_INITIAL_METADATA:
              gpr_free(op->data.send_initial_metadata.metadata);
              break;
            case GRPC_OP_SEND_MESSAGE:
              grpc_byte_buffer_destroy(op->data.send_message);
              break;
            case GRPC_OP_SEND_STATUS_FROM_SERVER:
              gpr_free(op->data.send_status_from_server.trailing_metadata);
              gpr_free((void*)op->data.send_status_from_server.status_details);
              break;
            case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
            case GRPC_OP_RECV_INITIAL_METADATA:
            case GRPC_OP_RECV_MESSAGE:
            case GRPC_OP_RECV_STATUS_ON_CLIENT:
            case GRPC_OP_RECV_CLOSE_ON_SERVER:
              break;
            }
          }
          gpr_free(ops);
        }
        break;
      }
      // cancel current call on client
      case 13: {
        if (num_calls > 0 && calls[0].client) {
          grpc_call_cancel(calls[0].client, NULL);
        } else {
          end(&inp);
        }
        break;
      }
      // cancel current call on server
      case 14: {
        if (num_calls > 0 && calls[0].server) {
          grpc_call_cancel(calls[0].server, NULL);
        } else {
          end(&inp);
        }
        break;
      }
      // get a calls peer on client
      case 15: {
        if (num_calls > 0 && calls[0].client) {
          free_non_null(grpc_call_get_peer(calls[0].client));
        } else {
          end(&inp);
        }
        break;
      }
      // get a calls peer on server
      case 16: {
        if (num_calls > 0 && calls[0].server) {
          free_non_null(grpc_call_get_peer(calls[0].server));
        } else {
          end(&inp);
        }
        break;
      }
      // get a channels target
      case 17: {
        if (g_channel != NULL) {
          free_non_null(grpc_channel_get_target(g_channel));
        } else {
          end(&inp);
        }
        break;
      }
      // send a ping on a channel
      case 18: {
        if (g_channel != NULL) {
          grpc_channel_ping(g_channel, cq,
                            create_validator(decrement, &pending_pings), NULL);
        } else {
          end(&inp);
        }
        break;
      }
      // enable a tracer
      case 19: {
        char *tracer = read_string(&inp);
        grpc_tracer_set_enabled(tracer, 1);
        gpr_free(tracer);
        break;
      }
      // disable a tracer
      case 20: {
        char *tracer = read_string(&inp);
        grpc_tracer_set_enabled(tracer, 0);
        gpr_free(tracer);
        break;
      }
    }
  }

  GPR_ASSERT(g_channel == NULL);
  GPR_ASSERT(g_server == NULL);

  grpc_completion_queue_shutdown(cq);
  GPR_ASSERT(
      grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), NULL)
          .type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);

  grpc_shutdown();
  return 0;
}
