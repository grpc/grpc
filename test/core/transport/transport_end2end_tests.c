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

#include "test/core/transport/transport_end2end_tests.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "src/core/support/string.h"
#include "src/core/transport/transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

static grpc_mdctx *g_metadata_context;

static gpr_once g_pending_ops_init = GPR_ONCE_INIT;
static gpr_mu g_mu;
static gpr_cv g_cv;
static int g_pending_ops;

/* Defines a suite of tests that all GRPC transports should be able to pass */

/******************************************************************************
 * Testing framework
 */

/* Forward declarations */
typedef struct test_fixture test_fixture;

/* User data passed to the transport and handed to each callback */
typedef struct test_user_data { test_fixture *fixture; } test_user_data;

/* A message we expect to receive (forms a singly linked list with next) */
typedef struct expected_message {
  /* The next message expected */
  struct expected_message *next;
  /* The (owned) data that we expect to receive */
  gpr_uint8 *data;
  /* The length of the expected message */
  size_t length;
  /* How many bytes of the expected message have we received? */
  size_t read_pos;
  /* Have we received the GRPC_OP_BEGIN for this message */
  int begun;
} expected_message;

/* Metadata we expect to receive */
typedef struct expected_metadata {
  struct expected_metadata *next;
  struct expected_metadata *prev;
  grpc_mdelem *metadata;
} expected_metadata;

/* Tracks a stream for a test. Forms a doubly-linked list with (prev, next) */
typedef struct test_stream {
  /* The owning fixture */
  test_fixture *fixture;
  /* The transport client stream */
  grpc_stream *client_stream;
  /* The transport server stream */
  grpc_stream *server_stream;
  /* Linked lists of messages expected on client and server */
  expected_message *client_expected_messages;
  expected_message *server_expected_messages;
  expected_metadata *client_expected_metadata;
  expected_metadata *server_expected_metadata;

  /* Test streams are linked in the fixture */
  struct test_stream *next;
  struct test_stream *prev;
} test_stream;

/* A test_fixture tracks all transport state and expectations for a test */
struct test_fixture {
  gpr_mu mu;
  gpr_cv cv; /* broadcast when expectation state has changed */

  /* The transport instances */
  grpc_transport *client_transport;
  grpc_transport *server_transport;
  /* User data for the transport instances - pointers to these are passed
     to the transport. */
  test_user_data client_ud;
  test_user_data server_ud;

  /* A pointer to the head of the tracked streams list, or NULL if no streams
     are open */
  test_stream *streams;
};

static void expect_metadata(test_stream *s, int from_client, const char *key,
                            const char *value);

/* Convert some number of seconds into a gpr_timespec that many seconds in the
   future */
static gpr_timespec deadline_from_seconds(double deadline_seconds) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(deadline_seconds);
}

/* Init a test_user_data instance */
static void init_user_data(test_user_data *ud, test_fixture *f,
                           grpc_transport_test_config *config, int is_client) {
  ud->fixture = f;
}

/* Implements the alloc_recv_buffer transport callback */
static gpr_slice alloc_recv_buffer(void *user_data, grpc_transport *transport,
                                   grpc_stream *stream, size_t size_hint) {
  return gpr_slice_malloc(size_hint);
}

static void pending_ops_cleanup(void) {
  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_cv);
}

static void pending_ops_init(void) {
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
  atexit(pending_ops_cleanup);
}

static void use_pending_ops(void) {
  gpr_once_init(&g_pending_ops_init, pending_ops_init);
}

static void add_pending_op(void) {
  use_pending_ops();
  gpr_mu_lock(&g_mu);
  g_pending_ops++;
  gpr_mu_unlock(&g_mu);
}

static void end_pending_op(void) {
  gpr_mu_lock(&g_mu);
  g_pending_ops--;
  gpr_cv_broadcast(&g_cv);
  gpr_mu_unlock(&g_mu);
}

static void wait_pending_ops(void) {
  use_pending_ops();
  gpr_mu_lock(&g_mu);
  while (g_pending_ops > 0) {
    gpr_cv_wait(&g_cv, &g_mu, gpr_inf_future);
  }
  gpr_mu_unlock(&g_mu);
}

/* Implements the create_stream transport callback */
static void create_stream(void *user_data, grpc_transport *transport,
                          const void *server_data) {
  test_user_data *ud = user_data;
  test_fixture *f = ud->fixture;
  test_stream *stream;

  GPR_ASSERT(ud == &f->server_ud);
  GPR_ASSERT(transport == f->server_transport);

  gpr_mu_lock(&f->mu);

  /* Search streams for the peer to this stream */
  if (!f->streams) goto done;
  /* found the expecting stream */
  stream = f->streams;
  stream->server_stream = gpr_malloc(grpc_transport_stream_size(transport));
  grpc_transport_init_stream(transport, stream->server_stream, server_data);

done:
  /* wakeup begin_stream, and maybe wait_and_verify */
  gpr_cv_broadcast(&f->cv);
  gpr_mu_unlock(&f->mu);
}

/* Search fixture streams for the test_stream instance holding a given transport
   stream */
static test_stream *find_test_stream(test_fixture *f, grpc_stream *stream) {
  test_stream *s;

  GPR_ASSERT(f->streams);
  s = f->streams;
  do {
    if (s->client_stream == stream || s->server_stream == stream) {
      return s;
    }
  } while (s != f->streams);

  GPR_ASSERT(0 && "found");
  return NULL;
}

/* Stringify a grpc_stream_state for debugging */
static const char *state_name(grpc_stream_state state) {
  switch (state) {
    case GRPC_STREAM_OPEN:
      return "GRPC_STREAM_OPEN";
    case GRPC_STREAM_RECV_CLOSED:
      return "GRPC_STREAM_RECV_CLOSED";
    case GRPC_STREAM_SEND_CLOSED:
      return "GRPC_STREAM_SEND_CLOSED";
    case GRPC_STREAM_CLOSED:
      return "GRPC_STREAM_CLOSED";
  }
  GPR_ASSERT(0 && "reachable");
  return NULL;
}

typedef struct {
  grpc_transport *transport;
  grpc_stream *stream;
} destroy_stream_args;

static void destroy_stream(void *p) {
  destroy_stream_args *a = p;
  grpc_transport_destroy_stream(a->transport, a->stream);
  gpr_free(a->stream);
  gpr_free(a);
  end_pending_op();
}

static void recv_batch(void *user_data, grpc_transport *transport,
                       grpc_stream *stream, grpc_stream_op *ops,
                       size_t ops_count, grpc_stream_state final_state) {
  test_user_data *ud = user_data;
  test_fixture *f = ud->fixture;
  test_stream *s;
  /* Pointer to the root pointer of either client or server expected messages;
     not a simple pointer as we may need to manipulate the list (on receipt
     of messages */
  expected_message **expect_root_message;
  expected_metadata **expect_root_metadata;
  expected_metadata *emd;
  size_t i, j;
  char *hexstr1, *hexstr2;
  int repeats = 0;

  gpr_mu_lock(&f->mu);

  s = find_test_stream(f, stream);
  expect_root_message = s->client_stream == stream
                            ? &s->client_expected_messages
                            : &s->server_expected_messages;
  expect_root_metadata = s->client_stream == stream
                             ? &s->client_expected_metadata
                             : &s->server_expected_metadata;

  /* Debug log */
  gpr_log(GPR_DEBUG, "recv_batch: %d ops on %s final_state=%s", ops_count,
          s->client_stream == stream ? "client" : "server",
          state_name(final_state));
#define CLEAR_REPEATS                           \
  if (repeats) {                                \
    gpr_log(GPR_DEBUG, "  + %d more", repeats); \
    repeats = 0;                                \
  }
  for (i = 0; i < ops_count; i++) {
    switch (ops[i].type) {
      case GRPC_NO_OP:
        CLEAR_REPEATS;
        gpr_log(GPR_DEBUG, "  [%02d] GRPC_NO_OP", i);
        break;
      case GRPC_OP_METADATA_BOUNDARY:
        CLEAR_REPEATS;
        gpr_log(GPR_DEBUG, "  [%02d] GRPC_OP_METADATA_BOUNDARY", i);
        break;
      case GRPC_OP_METADATA:
        CLEAR_REPEATS;
        hexstr1 =
            gpr_hexdump(grpc_mdstr_as_c_string(ops[i].data.metadata->key),
                        GPR_SLICE_LENGTH(ops[i].data.metadata->key->slice),
                        GPR_HEXDUMP_PLAINTEXT);
        hexstr2 =
            gpr_hexdump(grpc_mdstr_as_c_string(ops[i].data.metadata->value),
                        GPR_SLICE_LENGTH(ops[i].data.metadata->value->slice),
                        GPR_HEXDUMP_PLAINTEXT);
        gpr_log(GPR_DEBUG, "  [%02d] GRPC_OP_METADATA key=%s value=%s", i,
                hexstr1, hexstr2);
        gpr_free(hexstr1);
        gpr_free(hexstr2);
        break;
      case GRPC_OP_BEGIN_MESSAGE:
        CLEAR_REPEATS;
        gpr_log(GPR_DEBUG, "  [%02d] GRPC_OP_BEGIN_MESSAGE len=%d", i,
                ops[i].data.begin_message.length);
        break;
      case GRPC_OP_DEADLINE:
        CLEAR_REPEATS;
        gpr_log(GPR_DEBUG, "  [%02d] GRPC_OP_DEADLINE value=%d.%09d", i,
                ops[i].data.deadline.tv_sec, ops[i].data.deadline.tv_nsec);
        break;
      case GRPC_OP_SLICE:
        if (i && ops[i - 1].type == GRPC_OP_SLICE &&
            GPR_SLICE_LENGTH(ops[i - 1].data.slice) ==
                GPR_SLICE_LENGTH(ops[i].data.slice)) {
          repeats++;
        } else {
          CLEAR_REPEATS;
          gpr_log(GPR_DEBUG, "  [%02d] GRPC_OP_SLICE len=%d", i,
                  GPR_SLICE_LENGTH(ops[i].data.slice));
        }
        break;
      case GRPC_OP_FLOW_CTL_CB:
        CLEAR_REPEATS;
        gpr_log(GPR_DEBUG, "  [%02d] GRPC_OP_FLOW_CTL_CB", i);
        break;
    }
  }
  CLEAR_REPEATS;

  /* Iterate over operations, and verify them against expectations */
  for (i = 0; i < ops_count; i++) {
    switch (ops[i].type) {
      case GRPC_NO_OP:
        break;
      case GRPC_OP_METADATA_BOUNDARY:
        break;
      case GRPC_OP_METADATA:
        GPR_ASSERT(*expect_root_metadata && "must be expecting metadata");
        emd = *expect_root_metadata;
        if (emd == NULL) {
          gpr_log(GPR_ERROR, "metadata not found");
          abort();
        }
        do {
          if (emd->metadata == ops[i].data.metadata) {
            if (emd == *expect_root_metadata) {
              if (emd->next == emd) {
                *expect_root_metadata = NULL;
              } else {
                *expect_root_metadata = emd->next;
              }
            }
            emd->next->prev = emd->prev;
            emd->prev->next = emd->next;
            grpc_mdelem_unref(emd->metadata);
            grpc_mdelem_unref(ops[i].data.metadata);
            gpr_free(emd);
            emd = NULL;
            break;
          }
          emd = emd->next;
        } while (emd != *expect_root_metadata);
        if (emd) {
          gpr_log(GPR_ERROR, "metadata not found");
          abort();
        }
        break;
      case GRPC_OP_BEGIN_MESSAGE:
        GPR_ASSERT(*expect_root_message && "must be expecting a message");
        GPR_ASSERT((*expect_root_message)->read_pos == 0 &&
                   "must be at the start of a message");
        GPR_ASSERT((*expect_root_message)->begun == 0 &&
                   "can only BEGIN a message once");
        GPR_ASSERT((*expect_root_message)->length ==
                       ops[i].data.begin_message.length &&
                   "message lengths must match");
        (*expect_root_message)->begun = 1;
        break;
      case GRPC_OP_SLICE:
        GPR_ASSERT(*expect_root_message && "must be expecting a message");
        GPR_ASSERT((*expect_root_message)->begun == 1 &&
                   "must have begun a message");
        GPR_ASSERT((*expect_root_message)->read_pos +
                           GPR_SLICE_LENGTH(ops[i].data.slice) <=
                       (*expect_root_message)->length &&
                   "must not send more data than expected");
        for (j = 0; j < GPR_SLICE_LENGTH(ops[i].data.slice); j++) {
          GPR_ASSERT((*expect_root_message)
                             ->data[(*expect_root_message)->read_pos + j] ==
                         GPR_SLICE_START_PTR(ops[i].data.slice)[j] &&
                     "must send the correct message");
        }
        (*expect_root_message)->read_pos += GPR_SLICE_LENGTH(ops[i].data.slice);
        if ((*expect_root_message)->read_pos ==
            (*expect_root_message)->length) {
          expected_message *great_success = *expect_root_message;
          *expect_root_message = great_success->next;
          gpr_free(great_success->data);
          gpr_free(great_success);
        }
        gpr_slice_unref(ops[i].data.slice);
        break;
      case GRPC_OP_FLOW_CTL_CB:
        GPR_ASSERT(0 && "allowed");
        break;
      case GRPC_OP_DEADLINE:
        GPR_ASSERT(0 && "implemented");
        break;
    }
  }

  /* If the stream has become fully closed then we must destroy the transport
     part of the stream */
  if (final_state == GRPC_STREAM_CLOSED) {
    destroy_stream_args *dsa = gpr_malloc(sizeof(destroy_stream_args));
    gpr_thd_id id;
    dsa->transport = transport;
    dsa->stream = stream;
    /* start a thread after incrementing a pending op counter (so we can wait
       at test completion */
    add_pending_op();
    gpr_thd_new(&id, destroy_stream, dsa, NULL);
    if (stream == s->client_stream) {
      GPR_ASSERT(s->client_expected_messages == NULL &&
                 "must receive all expected messages");
      s->client_stream = NULL;
    } else {
      GPR_ASSERT(s->server_expected_messages == NULL &&
                 "must receive all expected messages");
      s->server_stream = NULL;
    }
    /* And if both the client and the server report fully closed, we can
       unlink the stream object entirely */
    if (s->client_stream == NULL && s->server_stream == NULL) {
      s->next->prev = s->prev;
      s->prev->next = s->next;
      if (s == f->streams) {
        if (s->next == f->streams) {
          f->streams = NULL;
        } else {
          f->streams = s->next;
        }
      }
    }
  }

  /* wakeup wait_and_verify */
  gpr_cv_broadcast(&f->cv);
  gpr_mu_unlock(&f->mu);
}

static void close_transport(void *user_data, grpc_transport *transport) {}

static void recv_goaway(void *user_data, grpc_transport *transport,
                        grpc_status_code status, gpr_slice debug) {
  gpr_slice_unref(debug);
}

static grpc_transport_callbacks transport_callbacks = {
    alloc_recv_buffer, create_stream, recv_batch, recv_goaway, close_transport};

/* Helper for tests to create a stream.
   Arguments:
     s - uninitialized test_stream struct to begin
     f - test fixture to associate this stream with
     method, host, deadline_seconds - header fields for the stream */
static void begin_stream(test_stream *s, test_fixture *f, const char *method,
                         const char *host, double deadline_seconds) {
  /* Deadline to initiate the stream (prevents the tests from hanging
     forever) */
  gpr_timespec deadline = deadline_from_seconds(10.0);
  grpc_stream_op_buffer sopb;

  grpc_sopb_init(&sopb);

  gpr_mu_lock(&f->mu);

  s->fixture = f;
  s->client_stream =
      gpr_malloc(grpc_transport_stream_size(f->client_transport));
  /* server stream will be set once it's received by the peer transport */
  s->server_stream = NULL;
  s->client_expected_messages = NULL;
  s->server_expected_messages = NULL;
  s->client_expected_metadata = NULL;
  s->server_expected_metadata = NULL;

  if (f->streams) {
    s->next = f->streams;
    s->prev = s->next->prev;
    s->next->prev = s->prev->next = s;
  } else {
    s->next = s->prev = s;
  }
  f->streams = s;

  gpr_mu_unlock(&f->mu);

  GPR_ASSERT(0 == grpc_transport_init_stream(f->client_transport,
                                             s->client_stream, NULL));

#define ADDMD(k, v)                                                           \
  do {                                                                        \
    grpc_mdelem *md = grpc_mdelem_from_strings(g_metadata_context, (k), (v)); \
    grpc_sopb_add_metadata(&sopb, md);                                        \
    expect_metadata(s, 1, (k), (v));                                          \
  } while (0)

  ADDMD(":path", method);
  ADDMD(":authority", host);
  ADDMD(":method", "POST");
  grpc_transport_send_batch(f->client_transport, s->client_stream, sopb.ops,
                            sopb.nops, 0);
  sopb.nops = 0;

  grpc_sopb_destroy(&sopb);

  /* wait for the server side stream to be created */
  gpr_mu_lock(&f->mu);
  while (s->server_stream == NULL) {
    GPR_ASSERT(0 == gpr_cv_wait(&f->cv, &f->mu, deadline));
  }
  gpr_mu_unlock(&f->mu);
}

static grpc_transport_setup_result setup_transport(
    test_fixture *f, grpc_transport **set_transport, void *user_data,
    grpc_transport *transport) {
  grpc_transport_setup_result result;

  gpr_mu_lock(&f->mu);
  *set_transport = transport;
  gpr_cv_broadcast(&f->cv);
  gpr_mu_unlock(&f->mu);

  result.callbacks = &transport_callbacks;
  result.user_data = user_data;
  return result;
}

static grpc_transport_setup_result setup_server_transport(
    void *arg, grpc_transport *transport, grpc_mdctx *mdctx) {
  test_fixture *f = arg;
  return setup_transport(f, &f->server_transport, &f->server_ud, transport);
}

static grpc_transport_setup_result setup_client_transport(
    void *arg, grpc_transport *transport, grpc_mdctx *mdctx) {
  test_fixture *f = arg;
  return setup_transport(f, &f->client_transport, &f->client_ud, transport);
}

/* Begin a test

   Arguments:
     f      - uninitialized test_fixture struct
     config - test configuration for this test
     name   - the name of this test */
static void begin_test(test_fixture *f, grpc_transport_test_config *config,
                       const char *name) {
  gpr_timespec timeout = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(100);

  gpr_log(GPR_INFO, "BEGIN: %s/%s", name, config->name);

  gpr_mu_init(&f->mu);
  gpr_cv_init(&f->cv);

  f->streams = NULL;

  init_user_data(&f->client_ud, f, config, 1);
  init_user_data(&f->server_ud, f, config, 0);

  f->client_transport = NULL;
  f->server_transport = NULL;

  GPR_ASSERT(0 ==
             config->create_transport(setup_client_transport, f,
                                      setup_server_transport, f,
                                      g_metadata_context));

  gpr_mu_lock(&f->mu);
  while (!f->client_transport || !f->server_transport) {
    GPR_ASSERT(gpr_cv_wait(&f->cv, &f->mu, timeout));
  }
  gpr_mu_unlock(&f->mu);
}

/* Enumerate expected messages on a stream */
static void enumerate_expected_messages(
    test_stream *s, expected_message *root, const char *stream_tag,
    void (*cb)(void *user, const char *fmt, ...), void *user) {
  expected_message *msg;

  for (msg = root; msg; msg = msg->next) {
    cb(user,
       "Waiting for message to finish: "
       "length=%zu read_pos=%zu begun=%d",
       msg->length, msg->read_pos);
  }
}

/* Walk through everything that is still waiting to happen, and call 'cb' with
   userdata 'user' for that expectation. */
static void enumerate_expectations(test_fixture *f,
                                   void (*cb)(void *user, const char *fmt, ...),
                                   void *user) {
  test_stream *stream;

  if (f->streams) {
    stream = f->streams;
    do {
      cb(user,
         "Waiting for request to close: "
         "client=%p, server=%p",
         stream->client_stream, stream->server_stream);
      enumerate_expected_messages(stream, stream->client_expected_messages,
                                  "client", cb, user);
      enumerate_expected_messages(stream, stream->server_expected_messages,
                                  "server", cb, user);
      stream = stream->next;
    } while (stream != f->streams);
  }
}

/* Callback for enumerate_expectations, that increments an integer each time
   an expectation is seen */
static void increment_expectation_count(void *p, const char *fmt, ...) {
  ++*(int *)p;
}

/* Returns the count of pending expectations in a fixture. Requires mu taken */
static int count_expectations(test_fixture *f) {
  int n = 0;
  enumerate_expectations(f, increment_expectation_count, &n);
  return n;
}

/* Callback for enumerate_expectations that adds an expectation to the log */
static void dump_expectation(void *p, const char *fmt, ...) {
  char *str;
  va_list args;
  va_start(args, fmt);

  gpr_asprintf(&str, fmt, args);
  gpr_log(GPR_INFO, "EXPECTED: %s", str);
  gpr_free(str);

  va_end(args);
}

/* Add all pending expectations to the log */
static void dump_expectations(test_fixture *f) {
  enumerate_expectations(f, dump_expectation, NULL);
}

/* Wait until all expectations are completed, or crash */
static void wait_and_verify(test_fixture *f) {
  gpr_timespec deadline = deadline_from_seconds(10.0);

  gpr_mu_lock(&f->mu);
  while (count_expectations(f) > 0) {
    gpr_log(GPR_INFO, "waiting for expectations to complete");
    if (gpr_cv_wait(&f->cv, &f->mu, deadline)) {
      gpr_log(GPR_ERROR, "Timeout waiting for expectation completion");
      dump_expectations(f);
      gpr_mu_unlock(&f->mu);
      abort();
    }
  }
  gpr_mu_unlock(&f->mu);
}

/* Finish a test */
static void end_test(test_fixture *f) {
  wait_and_verify(f);

  grpc_transport_close(f->client_transport);
  grpc_transport_close(f->server_transport);
  grpc_transport_destroy(f->client_transport);
  grpc_transport_destroy(f->server_transport);

  wait_pending_ops();
}

/* Generate a test slice filled with {0,1,2,3,...,255,0,1,2,3,4,...} */
static gpr_slice generate_test_data(size_t length) {
  gpr_slice slice = gpr_slice_malloc(length);
  size_t i;
  for (i = 0; i < length; i++) {
    GPR_SLICE_START_PTR(slice)[i] = i;
  }
  return slice;
}

/* Add an expected message to the end of a list with root root */
static void append_expected_message(expected_message **root,
                                    expected_message *message) {
  expected_message *end;

  if (!*root) {
    *root = message;
    return;
  }

  for (end = *root; end->next; end = end->next)
    ;
  end->next = message;
}

/* Add an expected message on stream 's''.
   If from_client==1, expect it on the server, otherwise expect it on the client
   Variadic parameters are a NULL-terminated list of pointers to slices that
   should be expected as payload */
static void expect_message(test_stream *s, int from_client,
                           /* gpr_slice* */...) {
  va_list args;
  gpr_slice *slice;
  size_t capacity = 32;
  size_t length = 0;
  gpr_uint8 *buffer = gpr_malloc(capacity);
  expected_message *e;

  va_start(args, from_client);
  while ((slice = va_arg(args, gpr_slice *))) {
    while (GPR_SLICE_LENGTH(*slice) + length > capacity) {
      capacity *= 2;
      buffer = gpr_realloc(buffer, capacity);
    }
    memcpy(buffer + length, GPR_SLICE_START_PTR(*slice),
           GPR_SLICE_LENGTH(*slice));
    length += GPR_SLICE_LENGTH(*slice);
  }
  va_end(args);

  e = gpr_malloc(sizeof(expected_message));
  e->data = buffer;
  e->length = length;
  e->read_pos = 0;
  e->begun = 0;
  e->next = NULL;

  gpr_mu_lock(&s->fixture->mu);
  append_expected_message(
      from_client ? &s->server_expected_messages : &s->client_expected_messages,
      e);
  gpr_mu_unlock(&s->fixture->mu);
}

static void expect_metadata(test_stream *s, int from_client, const char *key,
                            const char *value) {
  expected_metadata *e = gpr_malloc(sizeof(expected_metadata));
  expected_metadata **root =
      from_client ? &s->server_expected_metadata : &s->client_expected_metadata;
  e->metadata = grpc_mdelem_from_strings(g_metadata_context, key, value);
  gpr_mu_lock(&s->fixture->mu);
  if (!*root) {
    *root = e;
    e->next = e->prev = e;
  } else {
    e->next = *root;
    e->prev = e->next->prev;
    e->next->prev = e->prev->next = e;
  }
  gpr_mu_unlock(&s->fixture->mu);
}

/******************************************************************************
 * Actual unit tests
 */

/* Test that we can create, begin, and end a test */
static void test_no_op(grpc_transport_test_config *config) {
  test_fixture f;
  begin_test(&f, config, __FUNCTION__);
  end_test(&f);
}

/* Test that a request can be initiated and terminated normally */
static void test_simple_request(grpc_transport_test_config *config) {
  test_fixture f;
  test_stream s;

  begin_test(&f, config, __FUNCTION__);
  begin_stream(&s, &f, "/Test", "foo.google.com", 10);
  grpc_transport_send_batch(f.client_transport, s.client_stream, NULL, 0, 1);
  grpc_transport_send_batch(f.server_transport, s.server_stream, NULL, 0, 1);
  end_test(&f);
}

/* Test that a request can be aborted by the client */
static void test_can_abort_client(grpc_transport_test_config *config) {
  test_fixture f;
  test_stream s;

  begin_test(&f, config, __FUNCTION__);
  begin_stream(&s, &f, "/Test", "foo.google.com", 10);
  expect_metadata(&s, 0, "grpc-status", "1");
  expect_metadata(&s, 1, "grpc-status", "1");
  grpc_transport_abort_stream(f.client_transport, s.client_stream,
                              GRPC_STATUS_CANCELLED);
  end_test(&f);
}

/* Test that a request can be aborted by the server */
static void test_can_abort_server(grpc_transport_test_config *config) {
  test_fixture f;
  test_stream s;

  begin_test(&f, config, __FUNCTION__);
  begin_stream(&s, &f, "/Test", "foo.google.com", 10);
  expect_metadata(&s, 0, "grpc-status", "1");
  expect_metadata(&s, 1, "grpc-status", "1");
  grpc_transport_abort_stream(f.server_transport, s.server_stream,
                              GRPC_STATUS_CANCELLED);
  end_test(&f);
}

/* Test that a request can be sent with payload */
static void test_request_with_data(grpc_transport_test_config *config,
                                   size_t message_length) {
  test_fixture f;
  test_stream s;
  gpr_slice data = generate_test_data(message_length);
  grpc_stream_op_buffer sopb;

  grpc_sopb_init(&sopb);
  begin_test(&f, config, __FUNCTION__);
  gpr_log(GPR_INFO, "message_length = %d", message_length);
  begin_stream(&s, &f, "/Test", "foo.google.com", 10);
  expect_message(&s, 1, &data, NULL);
  grpc_sopb_add_begin_message(&sopb, message_length, 0);
  grpc_sopb_add_slice(&sopb, data);
  grpc_transport_set_allow_window_updates(f.server_transport, s.server_stream,
                                          1);
  grpc_transport_send_batch(f.client_transport, s.client_stream, sopb.ops,
                            sopb.nops, 1);
  sopb.nops = 0;
  grpc_transport_send_batch(f.server_transport, s.server_stream, NULL, 0, 1);
  end_test(&f);
  grpc_sopb_destroy(&sopb);
}

/* Increment an integer pointed to by x - used for verifying flow control */
static void increment_int(void *x, grpc_op_error error) { ++*(int *)x; }

/* Test that flow control callbacks are made at appropriate times */
static void test_request_with_flow_ctl_cb(grpc_transport_test_config *config,
                                          size_t message_length) {
  test_fixture f;
  test_stream s;
  int flow_ctl_called = 0;
  gpr_slice data = generate_test_data(message_length);
  grpc_stream_op_buffer sopb;

  grpc_sopb_init(&sopb);
  begin_test(&f, config, __FUNCTION__);
  gpr_log(GPR_INFO, "length=%d", message_length);
  begin_stream(&s, &f, "/Test", "foo.google.com", 10);
  expect_message(&s, 1, &data, NULL);
  grpc_sopb_add_begin_message(&sopb, message_length, 0);
  grpc_sopb_add_slice(&sopb, data);
  grpc_sopb_add_flow_ctl_cb(&sopb, increment_int, &flow_ctl_called);
  grpc_transport_set_allow_window_updates(f.server_transport, s.server_stream,
                                          1);
  grpc_transport_send_batch(f.client_transport, s.client_stream, sopb.ops,
                            sopb.nops, 1);
  sopb.nops = 0;
  grpc_transport_send_batch(f.server_transport, s.server_stream, NULL, 0, 1);
  end_test(&f);
  GPR_ASSERT(flow_ctl_called == 1);
  grpc_sopb_destroy(&sopb);
}

/* Set an event on ping response */
static void ping_cb(void *p) { gpr_event_set(p, (void *)1); }

/* Test that pinging gets a response */
static void test_ping(grpc_transport_test_config *config) {
  test_fixture f;
  gpr_event ev;

  begin_test(&f, config, __FUNCTION__);
  gpr_event_init(&ev);

  grpc_transport_ping(f.client_transport, ping_cb, &ev);
  GPR_ASSERT(gpr_event_wait(&ev, deadline_from_seconds(10)));

  end_test(&f);
}

/******************************************************************************
 * Test driver
 */

static const size_t interesting_message_lengths[] = {
    1, 100, 10000, 100000, 1000000,
};

void grpc_transport_end2end_tests(grpc_transport_test_config *config) {
  unsigned i;

  g_metadata_context = grpc_mdctx_create();

  test_no_op(config);
  test_simple_request(config);
  test_can_abort_client(config);
  test_can_abort_server(config);
  test_ping(config);
  for (i = 0; i < GPR_ARRAY_SIZE(interesting_message_lengths); i++) {
    test_request_with_data(config, interesting_message_lengths[i]);
    test_request_with_flow_ctl_cb(config, interesting_message_lengths[i]);
  }

  grpc_mdctx_unref(g_metadata_context);

  gpr_log(GPR_INFO, "tests completed ok");
}
