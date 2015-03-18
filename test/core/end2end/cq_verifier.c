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

#include "test/core/end2end/cq_verifier.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "src/core/surface/event_string.h"
#include "src/core/support/string.h"
#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

/* a set of metadata we expect to find on an event */
typedef struct metadata {
  size_t count;
  size_t cap;
  char **keys;
  char **values;
} metadata;

/* details what we expect to find on a single event - and forms a linked
   list to detail other expectations */
typedef struct expectation {
  struct expectation *next;
  struct expectation *prev;
  grpc_completion_type type;
  void *tag;
  union {
    grpc_op_error finish_accepted;
    grpc_op_error write_accepted;
    grpc_op_error op_complete;
    struct {
      const char *method;
      const char *host;
      gpr_timespec deadline;
      grpc_call **output_call;
      metadata *metadata;
    } server_rpc_new;
    metadata *client_metadata_read;
    struct {
      grpc_status_code status;
      const char *details;
      metadata *metadata;
    } finished;
    gpr_slice *read;
  } data;
} expectation;

/* the verifier itself */
struct cq_verifier {
  /* bound completion queue */
  grpc_completion_queue *cq;
  /* the root/sentinal expectation */
  expectation expect;
};

cq_verifier *cq_verifier_create(grpc_completion_queue *cq) {
  cq_verifier *v = gpr_malloc(sizeof(cq_verifier));
  v->expect.type = GRPC_COMPLETION_DO_NOT_USE;
  v->expect.tag = NULL;
  v->expect.next = &v->expect;
  v->expect.prev = &v->expect;
  v->cq = cq;
  return v;
}

void cq_verifier_destroy(cq_verifier *v) {
  cq_verify(v);
  gpr_free(v);
}

static int has_metadata(const grpc_metadata *md, size_t count, const char *key,
                        const char *value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (0 == strcmp(key, md[i].key) && strlen(value) == md[i].value_length &&
        0 == memcmp(md[i].value, value, md[i].value_length)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata(grpc_metadata_array *array, const char *key,
                      const char *value) {
  return has_metadata(array->metadata, array->count, key, value);
}

static void verify_and_destroy_metadata(metadata *md, grpc_metadata *elems,
                                        size_t count) {
  size_t i;
  for (i = 0; i < md->count; i++) {
    GPR_ASSERT(has_metadata(elems, count, md->keys[i], md->values[i]));
  }
  gpr_free(md->keys);
  gpr_free(md->values);
  gpr_free(md);
}

static gpr_slice merge_slices(gpr_slice *slices, size_t nslices) {
  size_t i;
  size_t len = 0;
  gpr_uint8 *cursor;
  gpr_slice out;

  for (i = 0; i < nslices; i++) {
    len += GPR_SLICE_LENGTH(slices[i]);
  }

  out = gpr_slice_malloc(len);
  cursor = GPR_SLICE_START_PTR(out);

  for (i = 0; i < nslices; i++) {
    memcpy(cursor, GPR_SLICE_START_PTR(slices[i]), GPR_SLICE_LENGTH(slices[i]));
    cursor += GPR_SLICE_LENGTH(slices[i]);
  }

  return out;
}

static int byte_buffer_eq_slice(grpc_byte_buffer *bb, gpr_slice b) {
  gpr_slice a =
      merge_slices(bb->data.slice_buffer.slices, bb->data.slice_buffer.count);
  int ok = GPR_SLICE_LENGTH(a) == GPR_SLICE_LENGTH(b) &&
           0 == memcmp(GPR_SLICE_START_PTR(a), GPR_SLICE_START_PTR(b),
                       GPR_SLICE_LENGTH(a));
  gpr_slice_unref(a);
  gpr_slice_unref(b);
  return ok;
}

int byte_buffer_eq_string(grpc_byte_buffer *bb, const char *str) {
  return byte_buffer_eq_slice(bb, gpr_slice_from_copied_string(str));
}

static int string_equivalent(const char *a, const char *b) {
  if (a == NULL) return b == NULL || b[0] == 0;
  if (b == NULL) return a[0] == 0;
  return strcmp(a, b) == 0;
}

static void verify_matches(expectation *e, grpc_event *ev) {
  GPR_ASSERT(e->type == ev->type);
  switch (e->type) {
    case GRPC_FINISH_ACCEPTED:
      GPR_ASSERT(e->data.finish_accepted == ev->data.finish_accepted);
      break;
    case GRPC_WRITE_ACCEPTED:
      GPR_ASSERT(e->data.write_accepted == ev->data.write_accepted);
      break;
    case GRPC_SERVER_RPC_NEW:
      GPR_ASSERT(string_equivalent(e->data.server_rpc_new.method,
                                   ev->data.server_rpc_new.method));
      GPR_ASSERT(string_equivalent(e->data.server_rpc_new.host,
                                   ev->data.server_rpc_new.host));
      GPR_ASSERT(gpr_time_cmp(e->data.server_rpc_new.deadline,
                              ev->data.server_rpc_new.deadline) <= 0);
      *e->data.server_rpc_new.output_call = ev->call;
      verify_and_destroy_metadata(e->data.server_rpc_new.metadata,
                                  ev->data.server_rpc_new.metadata_elements,
                                  ev->data.server_rpc_new.metadata_count);
      break;
    case GRPC_CLIENT_METADATA_READ:
      verify_and_destroy_metadata(e->data.client_metadata_read,
                                  ev->data.client_metadata_read.elements,
                                  ev->data.client_metadata_read.count);
      break;
    case GRPC_FINISHED:
      if (e->data.finished.status != GRPC_STATUS__DO_NOT_USE) {
        GPR_ASSERT(e->data.finished.status == ev->data.finished.status);
        GPR_ASSERT(string_equivalent(e->data.finished.details,
                                     ev->data.finished.details));
      }
      verify_and_destroy_metadata(e->data.finished.metadata,
                                  ev->data.finished.metadata_elements,
                                  ev->data.finished.metadata_count);
      break;
    case GRPC_QUEUE_SHUTDOWN:
      gpr_log(GPR_ERROR, "premature queue shutdown");
      abort();
      break;
    case GRPC_READ:
      if (e->data.read) {
        GPR_ASSERT(byte_buffer_eq_slice(ev->data.read, *e->data.read));
        gpr_free(e->data.read);
      } else {
        GPR_ASSERT(ev->data.read == NULL);
      }
      break;
    case GRPC_OP_COMPLETE:
      GPR_ASSERT(e->data.op_complete == ev->data.op_complete);
      break;
    case GRPC_SERVER_SHUTDOWN:
      break;
    case GRPC_COMPLETION_DO_NOT_USE:
      gpr_log(GPR_ERROR, "not implemented");
      abort();
      break;
  }
}

static void metadata_expectation(gpr_strvec *buf, metadata *md) {
  size_t i;
  char *tmp;

  if (!md) {
    gpr_strvec_add(buf, gpr_strdup("nil"));
  } else {
    for (i = 0; i < md->count; i++) {
      gpr_asprintf(&tmp, "%c%s:%s", i ? ',' : '{', md->keys[i], md->values[i]);
      gpr_strvec_add(buf, tmp);
    }
    if (md->count) {
      gpr_strvec_add(buf, gpr_strdup("}"));
    }
  }
}

static void expectation_to_strvec(gpr_strvec *buf, expectation *e) {
  gpr_timespec timeout;
  char *tmp;

  switch (e->type) {
    case GRPC_FINISH_ACCEPTED:
      gpr_asprintf(&tmp, "GRPC_FINISH_ACCEPTED result=%d",
                   e->data.finish_accepted);
      gpr_strvec_add(buf, tmp);
      break;
    case GRPC_WRITE_ACCEPTED:
      gpr_asprintf(&tmp, "GRPC_WRITE_ACCEPTED result=%d",
                   e->data.write_accepted);
      gpr_strvec_add(buf, tmp);
      break;
    case GRPC_OP_COMPLETE:
      gpr_asprintf(&tmp, "GRPC_OP_COMPLETE result=%d", e->data.op_complete);
      gpr_strvec_add(buf, tmp);
      break;
    case GRPC_SERVER_RPC_NEW:
      timeout = gpr_time_sub(e->data.server_rpc_new.deadline, gpr_now());
      gpr_asprintf(&tmp, "GRPC_SERVER_RPC_NEW method=%s host=%s timeout=%fsec",
                   e->data.server_rpc_new.method, e->data.server_rpc_new.host,
                   timeout.tv_sec + 1e-9 * timeout.tv_nsec);
      gpr_strvec_add(buf, tmp);
      break;
    case GRPC_CLIENT_METADATA_READ:
      gpr_strvec_add(buf, gpr_strdup("GRPC_CLIENT_METADATA_READ "));
      metadata_expectation(buf, e->data.client_metadata_read);
      break;
    case GRPC_FINISHED:
      gpr_asprintf(&tmp, "GRPC_FINISHED status=%d details=%s ",
                   e->data.finished.status, e->data.finished.details);
      gpr_strvec_add(buf, tmp);
      metadata_expectation(buf, e->data.finished.metadata);
      break;
    case GRPC_READ:
      gpr_strvec_add(buf, gpr_strdup("GRPC_READ data="));
      gpr_strvec_add(
          buf,
          gpr_hexdump((char *)GPR_SLICE_START_PTR(*e->data.read),
                      GPR_SLICE_LENGTH(*e->data.read), GPR_HEXDUMP_PLAINTEXT));
      break;
    case GRPC_SERVER_SHUTDOWN:
      gpr_strvec_add(buf, gpr_strdup("GRPC_SERVER_SHUTDOWN"));
      break;
    case GRPC_COMPLETION_DO_NOT_USE:
    case GRPC_QUEUE_SHUTDOWN:
      gpr_log(GPR_ERROR, "not implemented");
      abort();
      break;
  }
}

static void expectations_to_strvec(gpr_strvec *buf, cq_verifier *v) {
  expectation *e;

  for (e = v->expect.next; e != &v->expect; e = e->next) {
    expectation_to_strvec(buf, e);
    gpr_strvec_add(buf, gpr_strdup("\n"));
  }
}

static void fail_no_event_received(cq_verifier *v) {
  gpr_strvec buf;
  char *msg;
  gpr_strvec_init(&buf);
  gpr_strvec_add(&buf, gpr_strdup("no event received, but expected:\n"));
  expectations_to_strvec(&buf, v);
  msg = gpr_strvec_flatten(&buf, NULL);
  gpr_log(GPR_ERROR, "%s", msg);
  gpr_strvec_destroy(&buf);
  gpr_free(msg);
  abort();
}

void cq_verify(cq_verifier *v) {
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);
  grpc_event *ev;
  expectation *e;
  char *s;
  gpr_strvec have_tags;

  gpr_strvec_init(&have_tags);

  while (v->expect.next != &v->expect) {
    ev = grpc_completion_queue_next(v->cq, deadline);
    if (!ev) {
      fail_no_event_received(v);
    }

    for (e = v->expect.next; e != &v->expect; e = e->next) {
      gpr_asprintf(&s, " %p", e->tag);
      gpr_strvec_add(&have_tags, s);
      if (e->tag == ev->tag) {
        verify_matches(e, ev);
        e->next->prev = e->prev;
        e->prev->next = e->next;
        gpr_free(e);
        break;
      }
    }
    if (e == &v->expect) {
      s = grpc_event_string(ev);
      gpr_log(GPR_ERROR, "event not found: %s", s);
      gpr_free(s);
      s = gpr_strvec_flatten(&have_tags, NULL);
      gpr_log(GPR_ERROR, "have tags:%s", s);
      gpr_free(s);
      gpr_strvec_destroy(&have_tags);
      abort();
    }

    grpc_event_finish(ev);
  }

  gpr_strvec_destroy(&have_tags);
}

void cq_verify_empty(cq_verifier *v) {
  gpr_timespec deadline = gpr_time_add(gpr_now(), gpr_time_from_seconds(1));
  grpc_event *ev;

  GPR_ASSERT(v->expect.next == &v->expect && "expectation queue must be empty");

  ev = grpc_completion_queue_next(v->cq, deadline);
  if (ev != NULL) {
    char *s = grpc_event_string(ev);
    gpr_log(GPR_ERROR, "unexpected event (expected nothing): %s", s);
    gpr_free(s);
    abort();
  }
}

static expectation *add(cq_verifier *v, grpc_completion_type type, void *tag) {
  expectation *e = gpr_malloc(sizeof(expectation));
  e->type = type;
  e->tag = tag;
  e->next = &v->expect;
  e->prev = e->next->prev;
  e->next->prev = e->prev->next = e;
  return e;
}

static metadata *metadata_from_args(va_list args) {
  metadata *md = gpr_malloc(sizeof(metadata));
  const char *key, *value;
  md->count = 0;
  md->cap = 0;
  md->keys = NULL;
  md->values = NULL;

  for (;;) {
    key = va_arg(args, const char *);
    if (!key) return md;
    value = va_arg(args, const char *);
    GPR_ASSERT(value);

    if (md->cap == md->count) {
      md->cap = GPR_MAX(md->cap + 1, md->cap * 3 / 2);
      md->keys = gpr_realloc(md->keys, sizeof(char *) * md->cap);
      md->values = gpr_realloc(md->values, sizeof(char *) * md->cap);
    }
    md->keys[md->count] = (char *)key;
    md->values[md->count] = (char *)value;
    md->count++;
  }
}

void cq_expect_write_accepted(cq_verifier *v, void *tag, grpc_op_error result) {
  add(v, GRPC_WRITE_ACCEPTED, tag)->data.write_accepted = result;
}

void cq_expect_completion(cq_verifier *v, void *tag, grpc_op_error result) {
  add(v, GRPC_OP_COMPLETE, tag)->data.op_complete = result;
}

void cq_expect_finish_accepted(cq_verifier *v, void *tag,
                               grpc_op_error result) {
  add(v, GRPC_FINISH_ACCEPTED, tag)->data.finish_accepted = result;
}

void cq_expect_read(cq_verifier *v, void *tag, gpr_slice bytes) {
  expectation *e = add(v, GRPC_READ, tag);
  e->data.read = gpr_malloc(sizeof(gpr_slice));
  *e->data.read = bytes;
}

void cq_expect_empty_read(cq_verifier *v, void *tag) {
  expectation *e = add(v, GRPC_READ, tag);
  e->data.read = NULL;
}

void cq_expect_server_rpc_new(cq_verifier *v, grpc_call **output_call,
                              void *tag, const char *method, const char *host,
                              gpr_timespec deadline, ...) {
  va_list args;
  expectation *e = add(v, GRPC_SERVER_RPC_NEW, tag);
  e->data.server_rpc_new.method = method;
  e->data.server_rpc_new.host = host;
  e->data.server_rpc_new.deadline = deadline;
  e->data.server_rpc_new.output_call = output_call;

  va_start(args, deadline);
  e->data.server_rpc_new.metadata = metadata_from_args(args);
  va_end(args);
}

void cq_expect_client_metadata_read(cq_verifier *v, void *tag, ...) {
  va_list args;
  expectation *e = add(v, GRPC_CLIENT_METADATA_READ, tag);

  va_start(args, tag);
  e->data.client_metadata_read = metadata_from_args(args);
  va_end(args);
}

static void finished_internal(cq_verifier *v, void *tag,
                              grpc_status_code status, const char *details,
                              va_list args) {
  expectation *e = add(v, GRPC_FINISHED, tag);
  e->data.finished.status = status;
  e->data.finished.details = details;
  e->data.finished.metadata = metadata_from_args(args);
}

void cq_expect_finished_with_status(cq_verifier *v, void *tag,
                                    grpc_status_code status,
                                    const char *details, ...) {
  va_list args;
  va_start(args, details);
  finished_internal(v, tag, status, details, args);
  va_end(args);
}

void cq_expect_finished(cq_verifier *v, void *tag, ...) {
  va_list args;
  va_start(args, tag);
  finished_internal(v, tag, GRPC_STATUS__DO_NOT_USE, NULL, args);
  va_end(args);
}

void cq_expect_server_shutdown(cq_verifier *v, void *tag) {
  add(v, GRPC_SERVER_SHUTDOWN, tag);
}
