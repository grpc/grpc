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

static char *read_string(input_stream *inp) {
  size_t len = next_byte(inp);
  char *str = gpr_malloc(len + 1);
  for (size_t i = 0; i < len; i++) {
    str[i] = (char)next_byte(inp);
  }
  str[len] = 0;
  return str;
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

  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);

  while (!is_eof(&inp) || g_channel != NULL || g_server != NULL ||
         pending_channel_watches > 0) {
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
        inp.cur = inp.end;
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
        }
        break;
      }
      // destroy a channel
      case 3: {
        if (g_channel != NULL) {
          grpc_channel_destroy(g_channel);
          g_channel = NULL;
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
        }
        break;
      }
      // cancel all calls if shutdown
      case 6: {
        if (g_server != NULL && server_shutdown) {
          grpc_server_cancel_all_calls(g_server);
        }
        break;
      }
      // destroy server
      case 7: {
        if (g_server != NULL && server_shutdown &&
            pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = NULL;
        }
        break;
      }
      // check connectivity
      case 8: {
        if (g_channel != NULL) {
          grpc_channel_check_connectivity_state(g_channel,
                                                next_byte(&inp) > 127);
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
        }
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
