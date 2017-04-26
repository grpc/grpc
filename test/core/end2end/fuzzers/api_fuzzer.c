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
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/metadata.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/passthru_endpoint.h"

////////////////////////////////////////////////////////////////////////////////
// logging

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args *args) {}

////////////////////////////////////////////////////////////////////////////////
// global state

static gpr_timespec g_now;
static grpc_server *g_server;
static grpc_channel *g_channel;
static grpc_resource_quota *g_resource_quota;

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  return g_now;
}

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

static char *read_string(input_stream *inp, bool *special) {
  char *str = NULL;
  size_t cap = 0;
  size_t sz = 0;
  char c;
  do {
    if (cap == sz) {
      cap = GPR_MAX(3 * cap / 2, cap + 8);
      str = gpr_realloc(str, cap);
    }
    c = (char)next_byte(inp);
    str[sz++] = c;
  } while (c != 0 && c != 1);
  if (special != NULL) {
    *special = (c == 1);
  }
  if (c == 1) {
    str[sz - 1] = 0;
  }
  return str;
}

static void read_buffer(input_stream *inp, char **buffer, size_t *length,
                        bool *special) {
  *length = next_byte(inp);
  if (*length == 255) {
    if (special != NULL) *special = true;
    *length = next_byte(inp);
  } else {
    if (special != NULL) *special = false;
  }
  *buffer = gpr_malloc(*length);
  for (size_t i = 0; i < *length; i++) {
    (*buffer)[i] = (char)next_byte(inp);
  }
}

static grpc_slice maybe_intern(grpc_slice s, bool intern) {
  grpc_slice r = intern ? grpc_slice_intern(s) : grpc_slice_ref(s);
  grpc_slice_unref(s);
  return r;
}

static grpc_slice read_string_like_slice(input_stream *inp) {
  bool special;
  char *s = read_string(inp, &special);
  grpc_slice r = maybe_intern(grpc_slice_from_copied_string(s), special);
  gpr_free(s);
  return r;
}

static grpc_slice read_buffer_like_slice(input_stream *inp) {
  char *buffer;
  size_t length;
  bool special;
  read_buffer(inp, &buffer, &length, &special);
  grpc_slice r =
      maybe_intern(grpc_slice_from_copied_buffer(buffer, length), special);
  gpr_free(buffer);
  return r;
}

static uint32_t read_uint22(input_stream *inp) {
  uint8_t b = next_byte(inp);
  uint32_t x = b & 0x7f;
  if (b & 0x80) {
    x <<= 7;
    b = next_byte(inp);
    x |= b & 0x7f;
    if (b & 0x80) {
      x <<= 8;
      x |= next_byte(inp);
    }
  }
  return x;
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
  grpc_slice slice = grpc_slice_malloc(read_uint22(inp));
  memset(GRPC_SLICE_START_PTR(slice), 0, GRPC_SLICE_LENGTH(slice));
  grpc_byte_buffer *out = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return out;
}

static int read_int(input_stream *inp) { return (int)read_uint32(inp); }

static grpc_channel_args *read_args(input_stream *inp) {
  size_t n = next_byte(inp);
  grpc_arg *args = gpr_malloc(sizeof(*args) * n);
  for (size_t i = 0; i < n; i++) {
    switch (next_byte(inp)) {
      case 1:
        args[i].type = GRPC_ARG_STRING;
        args[i].key = read_string(inp, NULL);
        args[i].value.string = read_string(inp, NULL);
        break;
      case 2:
        args[i].type = GRPC_ARG_INTEGER;
        args[i].key = read_string(inp, NULL);
        args[i].value.integer = read_int(inp);
        break;
      case 3:
        args[i].type = GRPC_ARG_POINTER;
        args[i].key = gpr_strdup(GRPC_ARG_RESOURCE_QUOTA);
        args[i].value.pointer.vtable = grpc_resource_quota_arg_vtable();
        args[i].value.pointer.p = g_resource_quota;
        grpc_resource_quota_ref(g_resource_quota);
        break;
      default:
        end(inp);
        n = i;
        break;
    }
  }
  grpc_channel_args *a = gpr_malloc(sizeof(*a));
  a->args = args;
  a->num_args = n;
  return a;
}

typedef struct cred_artifact_ctx {
  int num_release;
  char *release[3];
} cred_artifact_ctx;
#define CRED_ARTIFACT_CTX_INIT \
  {                            \
    0, { 0 }                   \
  }

static void cred_artifact_ctx_finish(cred_artifact_ctx *ctx) {
  for (int i = 0; i < ctx->num_release; i++) {
    gpr_free(ctx->release[i]);
  }
}

static const char *read_cred_artifact(cred_artifact_ctx *ctx, input_stream *inp,
                                      const char **builtins,
                                      size_t num_builtins) {
  uint8_t b = next_byte(inp);
  if (b == 0) return NULL;
  if (b == 1) return ctx->release[ctx->num_release++] = read_string(inp, NULL);
  if (b >= num_builtins + 1) {
    end(inp);
    return NULL;
  }
  return builtins[b - 1];
}

static grpc_channel_credentials *read_ssl_channel_creds(input_stream *inp) {
  cred_artifact_ctx ctx = CRED_ARTIFACT_CTX_INIT;
  static const char *builtin_root_certs[] = {test_root_cert};
  static const char *builtin_private_keys[] = {
      test_server1_key, test_self_signed_client_key, test_signed_client_key};
  static const char *builtin_cert_chains[] = {
      test_server1_cert, test_self_signed_client_cert, test_signed_client_cert};
  const char *root_certs = read_cred_artifact(
      &ctx, inp, builtin_root_certs, GPR_ARRAY_SIZE(builtin_root_certs));
  const char *private_key = read_cred_artifact(
      &ctx, inp, builtin_private_keys, GPR_ARRAY_SIZE(builtin_private_keys));
  const char *certs = read_cred_artifact(&ctx, inp, builtin_cert_chains,
                                         GPR_ARRAY_SIZE(builtin_cert_chains));
  grpc_ssl_pem_key_cert_pair key_cert_pair = {private_key, certs};
  grpc_channel_credentials *creds = grpc_ssl_credentials_create(
      root_certs, private_key != NULL && certs != NULL ? &key_cert_pair : NULL,
      NULL);
  cred_artifact_ctx_finish(&ctx);
  return creds;
}

static grpc_call_credentials *read_call_creds(input_stream *inp) {
  switch (next_byte(inp)) {
    default:
      end(inp);
      return NULL;
    case 0:
      return NULL;
    case 1: {
      grpc_call_credentials *c1 = read_call_creds(inp);
      grpc_call_credentials *c2 = read_call_creds(inp);
      if (c1 != NULL && c2 != NULL) {
        grpc_call_credentials *out =
            grpc_composite_call_credentials_create(c1, c2, NULL);
        grpc_call_credentials_release(c1);
        grpc_call_credentials_release(c2);
        return out;
      } else if (c1 != NULL) {
        return c1;
      } else if (c2 != NULL) {
        return c2;
      } else {
        return NULL;
      }
      GPR_UNREACHABLE_CODE(return NULL);
    }
    case 2: {
      cred_artifact_ctx ctx = CRED_ARTIFACT_CTX_INIT;
      const char *access_token = read_cred_artifact(&ctx, inp, NULL, 0);
      grpc_call_credentials *out =
          access_token == NULL ? NULL : grpc_access_token_credentials_create(
                                            access_token, NULL);
      cred_artifact_ctx_finish(&ctx);
      return out;
    }
    case 3: {
      cred_artifact_ctx ctx = CRED_ARTIFACT_CTX_INIT;
      const char *auth_token = read_cred_artifact(&ctx, inp, NULL, 0);
      const char *auth_selector = read_cred_artifact(&ctx, inp, NULL, 0);
      grpc_call_credentials *out = auth_token == NULL || auth_selector == NULL
                                       ? NULL
                                       : grpc_google_iam_credentials_create(
                                             auth_token, auth_selector, NULL);
      cred_artifact_ctx_finish(&ctx);
      return out;
    }
      /* TODO(ctiller): more cred types here */
  }
}

static grpc_channel_credentials *read_channel_creds(input_stream *inp) {
  switch (next_byte(inp)) {
    case 0:
      return read_ssl_channel_creds(inp);
      break;
    case 1: {
      grpc_channel_credentials *c1 = read_channel_creds(inp);
      grpc_call_credentials *c2 = read_call_creds(inp);
      if (c1 != NULL && c2 != NULL) {
        grpc_channel_credentials *out =
            grpc_composite_channel_credentials_create(c1, c2, NULL);
        grpc_channel_credentials_release(c1);
        grpc_call_credentials_release(c2);
        return out;
      } else if (c1) {
        return c1;
      } else if (c2) {
        grpc_call_credentials_release(c2);
        return NULL;
      } else {
        return NULL;
      }
      GPR_UNREACHABLE_CODE(return NULL);
    }
    case 2:
      return NULL;
    default:
      end(inp);
      return NULL;
  }
}

static bool is_eof(input_stream *inp) { return inp->cur == inp->end; }

////////////////////////////////////////////////////////////////////////////////
// dns resolution

typedef struct addr_req {
  grpc_timer timer;
  char *addr;
  grpc_closure *on_done;
  grpc_resolved_addresses **addrs;
} addr_req;

static void finish_resolve(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  addr_req *r = arg;

  if (error == GRPC_ERROR_NONE && 0 == strcmp(r->addr, "server")) {
    grpc_resolved_addresses *addrs = gpr_malloc(sizeof(*addrs));
    addrs->naddrs = 1;
    addrs->addrs = gpr_malloc(sizeof(*addrs->addrs));
    addrs->addrs[0].len = 0;
    *r->addrs = addrs;
    grpc_closure_sched(exec_ctx, r->on_done, GRPC_ERROR_NONE);
  } else {
    grpc_closure_sched(exec_ctx, r->on_done,
                       GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                           "Resolution failed", &error, 1));
  }

  gpr_free(r->addr);
  gpr_free(r);
}

void my_resolve_address(grpc_exec_ctx *exec_ctx, const char *addr,
                        const char *default_port,
                        grpc_pollset_set *interested_parties,
                        grpc_closure *on_done,
                        grpc_resolved_addresses **addresses) {
  addr_req *r = gpr_malloc(sizeof(*r));
  r->addr = gpr_strdup(addr);
  r->on_done = on_done;
  r->addrs = addresses;
  grpc_timer_init(
      exec_ctx, &r->timer, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                        gpr_time_from_seconds(1, GPR_TIMESPAN)),
      grpc_closure_create(finish_resolve, r, grpc_schedule_on_exec_ctx),
      gpr_now(GPR_CLOCK_MONOTONIC));
}

////////////////////////////////////////////////////////////////////////////////
// client connection

// defined in tcp_client_posix.c
extern void (*grpc_tcp_client_connect_impl)(
    grpc_exec_ctx *exec_ctx, grpc_closure *closure, grpc_endpoint **ep,
    grpc_pollset_set *interested_parties, const grpc_channel_args *channel_args,
    const grpc_resolved_address *addr, gpr_timespec deadline);

static void sched_connect(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                          grpc_endpoint **ep, gpr_timespec deadline);

typedef struct {
  grpc_timer timer;
  grpc_closure *closure;
  grpc_endpoint **ep;
  gpr_timespec deadline;
} future_connect;

static void do_connect(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  future_connect *fc = arg;
  if (error != GRPC_ERROR_NONE) {
    *fc->ep = NULL;
    grpc_closure_sched(exec_ctx, fc->closure, GRPC_ERROR_REF(error));
  } else if (g_server != NULL) {
    grpc_endpoint *client;
    grpc_endpoint *server;
    grpc_passthru_endpoint_create(&client, &server, g_resource_quota, NULL);
    *fc->ep = client;

    grpc_transport *transport =
        grpc_create_chttp2_transport(exec_ctx, NULL, server, 0);
    grpc_server_setup_transport(exec_ctx, g_server, transport, NULL, NULL);
    grpc_chttp2_transport_start_reading(exec_ctx, transport, NULL);

    grpc_closure_sched(exec_ctx, fc->closure, GRPC_ERROR_NONE);
  } else {
    sched_connect(exec_ctx, fc->closure, fc->ep, fc->deadline);
  }
  gpr_free(fc);
}

static void sched_connect(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                          grpc_endpoint **ep, gpr_timespec deadline) {
  if (gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) < 0) {
    *ep = NULL;
    grpc_closure_sched(exec_ctx, closure, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                              "Connect deadline exceeded"));
    return;
  }

  future_connect *fc = gpr_malloc(sizeof(*fc));
  fc->closure = closure;
  fc->ep = ep;
  fc->deadline = deadline;
  grpc_timer_init(
      exec_ctx, &fc->timer, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                         gpr_time_from_millis(1, GPR_TIMESPAN)),
      grpc_closure_create(do_connect, fc, grpc_schedule_on_exec_ctx),
      gpr_now(GPR_CLOCK_MONOTONIC));
}

static void my_tcp_client_connect(grpc_exec_ctx *exec_ctx,
                                  grpc_closure *closure, grpc_endpoint **ep,
                                  grpc_pollset_set *interested_parties,
                                  const grpc_channel_args *channel_args,
                                  const grpc_resolved_address *addr,
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

typedef enum { ROOT, CLIENT, SERVER, PENDING_SERVER } call_state_type;

#define DONE_FLAG_CALL_CLOSED ((uint64_t)(1 << 0))

typedef struct call_state {
  call_state_type type;
  grpc_call *call;
  grpc_byte_buffer *recv_message;
  grpc_status_code status;
  grpc_metadata_array recv_initial_metadata;
  grpc_metadata_array recv_trailing_metadata;
  grpc_slice recv_status_details;
  int cancelled;
  int pending_ops;
  grpc_call_details call_details;
  grpc_byte_buffer *send_message;
  // starts at 0, individual flags from DONE_FLAG_xxx are set
  // as different operations are completed
  uint64_t done_flags;

  // array of pointers to free later
  size_t num_to_free;
  size_t cap_to_free;
  void **to_free;

  // array of slices to unref
  size_t num_slices_to_unref;
  size_t cap_slices_to_unref;
  grpc_slice **slices_to_unref;

  struct call_state *next;
  struct call_state *prev;
} call_state;

static call_state *g_active_call;

static call_state *new_call(call_state *sibling, call_state_type type) {
  call_state *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  if (sibling != NULL) {
    c->next = sibling;
    c->prev = sibling->prev;
    c->next->prev = c->prev->next = c;
  } else {
    c->next = c->prev = c;
  }
  c->type = type;
  return c;
}

static call_state *maybe_delete_call_state(call_state *call) {
  call_state *next = call->next;

  if (call->call != NULL) return next;
  if (call->pending_ops != 0) return next;

  if (call == g_active_call) {
    g_active_call = call->next;
    GPR_ASSERT(call != g_active_call);
  }

  call->prev->next = call->next;
  call->next->prev = call->prev;
  grpc_metadata_array_destroy(&call->recv_initial_metadata);
  grpc_metadata_array_destroy(&call->recv_trailing_metadata);
  grpc_slice_unref(call->recv_status_details);
  grpc_call_details_destroy(&call->call_details);

  for (size_t i = 0; i < call->num_slices_to_unref; i++) {
    grpc_slice_unref(*call->slices_to_unref[i]);
    gpr_free(call->slices_to_unref[i]);
  }
  for (size_t i = 0; i < call->num_to_free; i++) {
    gpr_free(call->to_free[i]);
  }
  gpr_free(call->to_free);
  gpr_free(call->slices_to_unref);

  gpr_free(call);

  return next;
}

static void add_to_free(call_state *call, void *p) {
  if (call->num_to_free == call->cap_to_free) {
    call->cap_to_free = GPR_MAX(8, 2 * call->cap_to_free);
    call->to_free =
        gpr_realloc(call->to_free, sizeof(*call->to_free) * call->cap_to_free);
  }
  call->to_free[call->num_to_free++] = p;
}

static grpc_slice *add_slice_to_unref(call_state *call, grpc_slice s) {
  if (call->num_slices_to_unref == call->cap_slices_to_unref) {
    call->cap_slices_to_unref = GPR_MAX(8, 2 * call->cap_slices_to_unref);
    call->slices_to_unref =
        gpr_realloc(call->slices_to_unref,
                    sizeof(*call->slices_to_unref) * call->cap_slices_to_unref);
  }
  call->slices_to_unref[call->num_slices_to_unref] =
      gpr_malloc(sizeof(grpc_slice));
  *call->slices_to_unref[call->num_slices_to_unref++] = s;
  return call->slices_to_unref[call->num_slices_to_unref - 1];
}

static void read_metadata(input_stream *inp, size_t *count,
                          grpc_metadata **metadata, call_state *cs) {
  *count = next_byte(inp);
  if (*count) {
    *metadata = gpr_malloc(*count * sizeof(**metadata));
    memset(*metadata, 0, *count * sizeof(**metadata));
    for (size_t i = 0; i < *count; i++) {
      (*metadata)[i].key = read_string_like_slice(inp);
      (*metadata)[i].value = read_buffer_like_slice(inp);
      (*metadata)[i].flags = read_uint32(inp);
      add_slice_to_unref(cs, (*metadata)[i].key);
      add_slice_to_unref(cs, (*metadata)[i].value);
    }
  } else {
    *metadata = gpr_malloc(1);
  }
  add_to_free(cs, *metadata);
}

static call_state *destroy_call(call_state *call) {
  grpc_call_destroy(call->call);
  call->call = NULL;
  return maybe_delete_call_state(call);
}

static void finished_request_call(void *csp, bool success) {
  call_state *cs = csp;
  GPR_ASSERT(cs->pending_ops > 0);
  --cs->pending_ops;
  if (success) {
    GPR_ASSERT(cs->call != NULL);
    cs->type = SERVER;
  } else {
    maybe_delete_call_state(cs);
  }
}

typedef struct {
  call_state *cs;
  uint8_t has_ops;
} batch_info;

static void finished_batch(void *p, bool success) {
  batch_info *bi = p;
  --bi->cs->pending_ops;
  if ((bi->has_ops & (1u << GRPC_OP_RECV_MESSAGE)) &&
      (bi->cs->done_flags & DONE_FLAG_CALL_CLOSED)) {
    GPR_ASSERT(bi->cs->recv_message == NULL);
  }
  if ((bi->has_ops & (1u << GRPC_OP_RECV_MESSAGE) &&
       bi->cs->recv_message != NULL)) {
    grpc_byte_buffer_destroy(bi->cs->recv_message);
    bi->cs->recv_message = NULL;
  }
  if ((bi->has_ops & (1u << GRPC_OP_SEND_MESSAGE))) {
    grpc_byte_buffer_destroy(bi->cs->send_message);
    bi->cs->send_message = NULL;
  }
  if ((bi->has_ops & (1u << GRPC_OP_RECV_STATUS_ON_CLIENT)) ||
      (bi->has_ops & (1u << GRPC_OP_RECV_CLOSE_ON_SERVER))) {
    bi->cs->done_flags |= DONE_FLAG_CALL_CLOSED;
  }
  maybe_delete_call_state(bi->cs);
  gpr_free(bi);
}

static validator *make_finished_batch_validator(call_state *cs,
                                                uint8_t has_ops) {
  batch_info *bi = gpr_malloc(sizeof(*bi));
  bi->cs = cs;
  bi->has_ops = has_ops;
  return create_validator(finished_batch, bi);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  grpc_test_only_set_slice_hash_seed(0);
  if (squelch) gpr_set_log_function(dont_log);
  input_stream inp = {data, data + size};
  grpc_tcp_client_connect_impl = my_tcp_client_connect;
  gpr_now_impl = now_impl;
  grpc_init();
  grpc_resolve_address = my_resolve_address;

  GPR_ASSERT(g_channel == NULL);
  GPR_ASSERT(g_server == NULL);

  bool server_shutdown = false;
  int pending_server_shutdowns = 0;
  int pending_channel_watches = 0;
  int pending_pings = 0;

  g_active_call = new_call(NULL, ROOT);
  g_resource_quota = grpc_resource_quota_create("api_fuzzer");

  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);

  while (!is_eof(&inp) || g_channel != NULL || g_server != NULL ||
         pending_channel_watches > 0 || pending_pings > 0 ||
         g_active_call->type != ROOT || g_active_call->next != g_active_call) {
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
      call_state *s = g_active_call;
      do {
        if (s->type != PENDING_SERVER && s->call != NULL) {
          s = destroy_call(s);
        } else {
          s = s->next;
        }
      } while (s != g_active_call);

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
          char *target = read_string(&inp, NULL);
          char *target_uri;
          gpr_asprintf(&target_uri, "dns:%s", target);
          grpc_channel_args *args = read_args(&inp);
          g_channel = grpc_insecure_channel_create(target_uri, args, NULL);
          GPR_ASSERT(g_channel != NULL);
          {
            grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
            grpc_channel_args_destroy(&exec_ctx, args);
            grpc_exec_ctx_finish(&exec_ctx);
          }
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
          {
            grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
            grpc_channel_args_destroy(&exec_ctx, args);
            grpc_exec_ctx_finish(&exec_ctx);
          }
          grpc_server_register_completion_queue(g_server, cq, NULL);
          grpc_server_start(g_server);
          server_shutdown = false;
          GPR_ASSERT(pending_server_shutdowns == 0);
        } else {
          end(&inp);
        }
        break;
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
          if (st != GRPC_CHANNEL_SHUTDOWN) {
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
        grpc_call *parent_call = NULL;
        if (g_active_call->type != ROOT) {
          if (g_active_call->call == NULL || g_active_call->type == CLIENT) {
            end(&inp);
            break;
          }
          parent_call = g_active_call->call;
        }
        uint32_t propagation_mask = read_uint32(&inp);
        grpc_slice method = read_string_like_slice(&inp);
        grpc_slice host = read_string_like_slice(&inp);
        gpr_timespec deadline =
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(read_uint32(&inp), GPR_TIMESPAN));

        if (ok) {
          call_state *cs = new_call(g_active_call, CLIENT);
          cs->call =
              grpc_channel_create_call(g_channel, parent_call, propagation_mask,
                                       cq, method, &host, deadline, NULL);
        } else {
          end(&inp);
        }
        grpc_slice_unref(method);
        grpc_slice_unref(host);
        break;
      }
      // switch the 'current' call
      case 11: {
        g_active_call = g_active_call->next;
        break;
      }
      // queue some ops on a call
      case 12: {
        if (g_active_call->type == PENDING_SERVER ||
            g_active_call->type == ROOT || g_active_call->call == NULL) {
          end(&inp);
          break;
        }
        size_t num_ops = next_byte(&inp);
        if (num_ops > 6) {
          end(&inp);
          break;
        }
        grpc_op *ops = gpr_malloc(sizeof(grpc_op) * num_ops);
        memset(ops, 0, sizeof(grpc_op) * num_ops);
        bool ok = true;
        size_t i;
        grpc_op *op;
        uint8_t has_ops = 0;
        for (i = 0; i < num_ops; i++) {
          op = &ops[i];
          switch (next_byte(&inp)) {
            default:
              /* invalid value */
              op->op = (grpc_op_type)-1;
              ok = false;
              break;
            case GRPC_OP_SEND_INITIAL_METADATA:
              op->op = GRPC_OP_SEND_INITIAL_METADATA;
              has_ops |= 1 << GRPC_OP_SEND_INITIAL_METADATA;
              read_metadata(&inp, &op->data.send_initial_metadata.count,
                            &op->data.send_initial_metadata.metadata,
                            g_active_call);
              break;
            case GRPC_OP_SEND_MESSAGE:
              op->op = GRPC_OP_SEND_MESSAGE;
              if (g_active_call->send_message != NULL) {
                ok = false;
              } else {
                has_ops |= 1 << GRPC_OP_SEND_MESSAGE;
                g_active_call->send_message =
                    op->data.send_message.send_message = read_message(&inp);
              }
              break;
            case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
              op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
              has_ops |= 1 << GRPC_OP_SEND_CLOSE_FROM_CLIENT;
              break;
            case GRPC_OP_SEND_STATUS_FROM_SERVER:
              op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
              has_ops |= 1 << GRPC_OP_SEND_STATUS_FROM_SERVER;
              read_metadata(
                  &inp,
                  &op->data.send_status_from_server.trailing_metadata_count,
                  &op->data.send_status_from_server.trailing_metadata,
                  g_active_call);
              op->data.send_status_from_server.status = next_byte(&inp);
              op->data.send_status_from_server.status_details =
                  add_slice_to_unref(g_active_call,
                                     read_buffer_like_slice(&inp));
              break;
            case GRPC_OP_RECV_INITIAL_METADATA:
              op->op = GRPC_OP_RECV_INITIAL_METADATA;
              has_ops |= 1 << GRPC_OP_RECV_INITIAL_METADATA;
              op->data.recv_initial_metadata.recv_initial_metadata =
                  &g_active_call->recv_initial_metadata;
              break;
            case GRPC_OP_RECV_MESSAGE:
              op->op = GRPC_OP_RECV_MESSAGE;
              has_ops |= 1 << GRPC_OP_RECV_MESSAGE;
              op->data.recv_message.recv_message = &g_active_call->recv_message;
              break;
            case GRPC_OP_RECV_STATUS_ON_CLIENT:
              op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
              op->data.recv_status_on_client.status = &g_active_call->status;
              op->data.recv_status_on_client.trailing_metadata =
                  &g_active_call->recv_trailing_metadata;
              op->data.recv_status_on_client.status_details =
                  &g_active_call->recv_status_details;
              break;
            case GRPC_OP_RECV_CLOSE_ON_SERVER:
              op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
              has_ops |= 1 << GRPC_OP_RECV_CLOSE_ON_SERVER;
              op->data.recv_close_on_server.cancelled =
                  &g_active_call->cancelled;
              break;
          }
          op->reserved = NULL;
          op->flags = read_uint32(&inp);
        }
        if (ok) {
          validator *v = make_finished_batch_validator(g_active_call, has_ops);
          g_active_call->pending_ops++;
          grpc_call_error error =
              grpc_call_start_batch(g_active_call->call, ops, num_ops, v, NULL);
          if (error != GRPC_CALL_OK) {
            v->validate(v->arg, false);
            gpr_free(v);
          }
        } else {
          end(&inp);
        }
        if (!ok && (has_ops & (1 << GRPC_OP_SEND_MESSAGE))) {
          grpc_byte_buffer_destroy(g_active_call->send_message);
          g_active_call->send_message = NULL;
        }
        gpr_free(ops);

        break;
      }
      // cancel current call
      case 13: {
        if (g_active_call->type != ROOT && g_active_call->call != NULL) {
          grpc_call_cancel(g_active_call->call, NULL);
        } else {
          end(&inp);
        }
        break;
      }
      // get a calls peer
      case 14: {
        if (g_active_call->type != ROOT && g_active_call->call != NULL) {
          free_non_null(grpc_call_get_peer(g_active_call->call));
        } else {
          end(&inp);
        }
        break;
      }
      // get a channels target
      case 15: {
        if (g_channel != NULL) {
          free_non_null(grpc_channel_get_target(g_channel));
        } else {
          end(&inp);
        }
        break;
      }
      // send a ping on a channel
      case 16: {
        if (g_channel != NULL) {
          pending_pings++;
          grpc_channel_ping(g_channel, cq,
                            create_validator(decrement, &pending_pings), NULL);
        } else {
          end(&inp);
        }
        break;
      }
      // enable a tracer
      case 17: {
        char *tracer = read_string(&inp, NULL);
        grpc_tracer_set_enabled(tracer, 1);
        gpr_free(tracer);
        break;
      }
      // disable a tracer
      case 18: {
        char *tracer = read_string(&inp, NULL);
        grpc_tracer_set_enabled(tracer, 0);
        gpr_free(tracer);
        break;
      }
      // request a server call
      case 19: {
        if (g_server == NULL) {
          end(&inp);
          break;
        }
        call_state *cs = new_call(g_active_call, PENDING_SERVER);
        cs->pending_ops++;
        validator *v = create_validator(finished_request_call, cs);
        grpc_call_error error =
            grpc_server_request_call(g_server, &cs->call, &cs->call_details,
                                     &cs->recv_initial_metadata, cq, cq, v);
        if (error != GRPC_CALL_OK) {
          v->validate(v->arg, false);
          gpr_free(v);
        }
        break;
      }
      // destroy a call
      case 20: {
        if (g_active_call->type != ROOT &&
            g_active_call->type != PENDING_SERVER &&
            g_active_call->call != NULL) {
          destroy_call(g_active_call);
        } else {
          end(&inp);
        }
        break;
      }
      // resize the buffer pool
      case 21: {
        grpc_resource_quota_resize(g_resource_quota, read_uint22(&inp));
        break;
      }
      // create a secure channel
      case 22: {
        if (g_channel == NULL) {
          char *target = read_string(&inp, NULL);
          char *target_uri;
          gpr_asprintf(&target_uri, "dns:%s", target);
          grpc_channel_args *args = read_args(&inp);
          grpc_channel_credentials *creds = read_channel_creds(&inp);
          g_channel = grpc_secure_channel_create(creds, target_uri, args, NULL);
          GPR_ASSERT(g_channel != NULL);
          {
            grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
            grpc_channel_args_destroy(&exec_ctx, args);
            grpc_exec_ctx_finish(&exec_ctx);
          }
          gpr_free(target_uri);
          gpr_free(target);
          grpc_channel_credentials_release(creds);
        } else {
          end(&inp);
        }
        break;
      }
    }
  }

  GPR_ASSERT(g_channel == NULL);
  GPR_ASSERT(g_server == NULL);
  GPR_ASSERT(g_active_call->type == ROOT);
  GPR_ASSERT(g_active_call->next == g_active_call);
  gpr_free(g_active_call);

  grpc_completion_queue_shutdown(cq);
  GPR_ASSERT(
      grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), NULL)
          .type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);

  grpc_resource_quota_unref(g_resource_quota);

  grpc_shutdown();
  return 0;
}
