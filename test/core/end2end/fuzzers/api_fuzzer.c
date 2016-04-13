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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/transport/metadata.h"
#include "test/core/util/mock_endpoint.h"

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

static gpr_mu g_mu;
static gpr_cv g_cv;
static gpr_timespec g_now;

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  gpr_mu_lock(&g_mu);
  gpr_timespec now = g_now;
  gpr_cv_broadcast(&g_cv);
  gpr_mu_unlock(&g_mu);
  return now;
}

static void wait_until(gpr_timespec when) {
  gpr_mu_lock(&g_mu);
  while (gpr_time_cmp(when, g_now) < 0) {
    gpr_cv_wait(&g_cv, &g_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&g_mu);
}

////////////////////////////////////////////////////////////////////////////////
// dns resolution

static grpc_resolved_addresses *my_resolve_address(const char *name,
                                                   const char *default_port) {
  if (0 == strcmp(name, "server")) {
    wait_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                            gpr_time_from_seconds(1, GPR_TIMESPAN)));
    grpc_resolved_addresses *addrs = gpr_malloc(sizeof(*addrs));
    addrs->naddrs = 1;
    addrs->addrs = gpr_malloc(sizeof(*addrs->addrs));
    addrs->addrs[0].len = 0;
    return addrs;
  } else if (0 == strcmp(name, "wait")) {
    wait_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                            gpr_time_from_seconds(1, GPR_TIMESPAN)));
    return NULL;
  } else {
    return NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
// test state

typedef struct { grpc_channel *channel; } channel_state;
typedef struct { grpc_server *server; } server_state;

////////////////////////////////////////////////////////////////////////////////
// test driver

typedef enum {
  SERVER_SHUTDOWN,
} tag_name;

static void *tag(tag_name name) { return (void *)(uintptr_t)name; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  grpc_test_only_set_metadata_hash_seed(0);
  if (squelch) gpr_set_log_function(dont_log);
  input_stream inp = {data, data + size};
  grpc_blocking_resolve_address = my_resolve_address;
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
  gpr_now_impl = now_impl;
  grpc_init();

  grpc_channel *channel = NULL;
  grpc_server *server = NULL;
  bool server_shutdown = false;
  int pending_server_shutdowns = 0;

  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);

  while (!is_eof(&inp) || channel != NULL || server != NULL) {
    if (is_eof(&inp)) {
      if (channel != NULL) {
        grpc_channel_destroy(channel);
        channel = NULL;
      }
      if (server != NULL) {
        if (!server_shutdown) {
          grpc_server_shutdown_and_notify(server, cq, tag(SERVER_SHUTDOWN));
          server_shutdown = true;
          pending_server_shutdowns++;
        } else if (pending_server_shutdowns == 0) {
          grpc_server_destroy(server);
          server = NULL;
        }
      }

      gpr_mu_lock(&g_mu);
      g_now = gpr_time_add(g_now, gpr_time_from_seconds(1, GPR_TIMESPAN));
      gpr_cv_broadcast(&g_cv);
      gpr_mu_unlock(&g_mu);
    }

    switch (next_byte(&inp)) {
      // tickle completion queue
      case 0: {
        grpc_event ev = grpc_completion_queue_next(
            cq, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
        switch (ev.type) {
          case GRPC_OP_COMPLETE:
            switch ((tag_name)(uintptr_t)ev.tag) {
              case SERVER_SHUTDOWN:
                GPR_ASSERT(pending_server_shutdowns);
                pending_server_shutdowns--;
                break;
              default:
                GPR_ASSERT(false);
            }
            break;
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
        gpr_mu_lock(&g_mu);
        g_now = gpr_time_add(
            g_now, gpr_time_from_micros(read_uint32(&inp), GPR_TIMESPAN));
        gpr_mu_unlock(&g_mu);
        break;
      }
      // create an insecure channel
      case 2: {
        if (channel == NULL) {
          char *target = read_string(&inp);
          char *target_uri;
          gpr_asprintf(&target_uri, "dns:%s", target);
          grpc_channel_args *args = read_args(&inp);
          channel = grpc_insecure_channel_create(target_uri, args, NULL);
          GPR_ASSERT(channel != NULL);
          grpc_channel_args_destroy(args);
          gpr_free(target_uri);
          gpr_free(target);
        }
        break;
      }
      // destroy a channel
      case 3: {
        if (channel != NULL) {
          grpc_channel_destroy(channel);
          channel = NULL;
        }
        break;
      }
      // bring up a server
      case 4: {
        if (server == NULL) {
          grpc_channel_args *args = read_args(&inp);
          server = grpc_server_create(args, NULL);
          GPR_ASSERT(server != NULL);
          grpc_channel_args_destroy(args);
          grpc_server_register_completion_queue(server, cq, NULL);
          grpc_server_start(server);
          server_shutdown = false;
          GPR_ASSERT(pending_server_shutdowns == 0);
        }
      }
      // begin server shutdown
      case 5: {
        if (server != NULL) {
          grpc_server_shutdown_and_notify(server, cq, tag(SERVER_SHUTDOWN));
          pending_server_shutdowns++;
          server_shutdown = true;
        }
        break;
      }
      // cancel all calls if shutdown
      case 6: {
        if (server != NULL && server_shutdown) {
          grpc_server_cancel_all_calls(server);
        }
        break;
      }
      // destroy server
      case 7: {
        if (server != NULL && server_shutdown &&
            pending_server_shutdowns == 0) {
          grpc_server_destroy(server);
          server = NULL;
        }
        break;
      }
    }
  }

  grpc_completion_queue_shutdown(cq);
  GPR_ASSERT(
      grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), NULL)
          .type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_cv);
  return 0;
}
