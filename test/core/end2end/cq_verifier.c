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

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/event_string.h"

#define ROOT_EXPECTATION 1000

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
  int success;
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
  v->expect.type = ROOT_EXPECTATION;
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

static gpr_slice merge_slices(gpr_slice *slices, size_t nslices) {
  size_t i;
  size_t len = 0;
  uint8_t *cursor;
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
  gpr_slice a;
  int ok;

  if (!bb) return 0;

  a = merge_slices(bb->data.raw.slice_buffer.slices,
                   bb->data.raw.slice_buffer.count);
  ok = GPR_SLICE_LENGTH(a) == GPR_SLICE_LENGTH(b) &&
       0 == memcmp(GPR_SLICE_START_PTR(a), GPR_SLICE_START_PTR(b),
                   GPR_SLICE_LENGTH(a));
  gpr_slice_unref(a);
  gpr_slice_unref(b);
  return ok;
}

int byte_buffer_eq_string(grpc_byte_buffer *bb, const char *str) {
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer *rbb;
  int res;

  grpc_byte_buffer_reader_init(&reader, bb);
  rbb = grpc_raw_byte_buffer_from_reader(&reader);
  res = byte_buffer_eq_slice(rbb, gpr_slice_from_copied_string(str));
  grpc_byte_buffer_reader_destroy(&reader);
  grpc_byte_buffer_destroy(rbb);

  return res;
}

static void verify_matches(expectation *e, grpc_event *ev) {
  GPR_ASSERT(e->type == ev->type);
  switch (e->type) {
    case GRPC_QUEUE_SHUTDOWN:
      gpr_log(GPR_ERROR, "premature queue shutdown");
      abort();
      break;
    case GRPC_OP_COMPLETE:
      GPR_ASSERT(e->success == ev->success);
      break;
    case GRPC_QUEUE_TIMEOUT:
      gpr_log(GPR_ERROR, "not implemented");
      abort();
      break;
  }
}

static void expectation_to_strvec(gpr_strvec *buf, expectation *e) {
  char *tmp;

  gpr_asprintf(&tmp, "%p ", e->tag);
  gpr_strvec_add(buf, tmp);

  switch (e->type) {
    case GRPC_OP_COMPLETE:
      gpr_asprintf(&tmp, "GRPC_OP_COMPLETE result=%d", e->success);
      gpr_strvec_add(buf, tmp);
      break;
    case GRPC_QUEUE_TIMEOUT:
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
  grpc_event ev;
  expectation *e;
  char *s;
  gpr_strvec have_tags;

  gpr_strvec_init(&have_tags);

  while (v->expect.next != &v->expect) {
    ev = grpc_completion_queue_next(v->cq, deadline, NULL);
    if (ev.type == GRPC_QUEUE_TIMEOUT) {
      fail_no_event_received(v);
      break;
    }

    for (e = v->expect.next; e != &v->expect; e = e->next) {
      gpr_asprintf(&s, " %p", e->tag);
      gpr_strvec_add(&have_tags, s);
      if (e->tag == ev.tag) {
        verify_matches(e, &ev);
        e->next->prev = e->prev;
        e->prev->next = e->next;
        gpr_free(e);
        break;
      }
    }
    if (e == &v->expect) {
      s = grpc_event_string(&ev);
      gpr_log(GPR_ERROR, "event not found: %s", s);
      gpr_free(s);
      s = gpr_strvec_flatten(&have_tags, NULL);
      gpr_log(GPR_ERROR, "have tags:%s", s);
      gpr_free(s);
      gpr_strvec_destroy(&have_tags);
      abort();
    }
  }

  gpr_strvec_destroy(&have_tags);
}

void cq_verify_empty_timeout(cq_verifier *v, int timeout_sec) {
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_seconds(timeout_sec, GPR_TIMESPAN));
  grpc_event ev;

  GPR_ASSERT(v->expect.next == &v->expect && "expectation queue must be empty");

  ev = grpc_completion_queue_next(v->cq, deadline, NULL);
  if (ev.type != GRPC_QUEUE_TIMEOUT) {
    char *s = grpc_event_string(&ev);
    gpr_log(GPR_ERROR, "unexpected event (expected nothing): %s", s);
    gpr_free(s);
    abort();
  }
}

void cq_verify_empty(cq_verifier *v) { cq_verify_empty_timeout(v, 1); }

static expectation *add(cq_verifier *v, grpc_completion_type type, void *tag) {
  expectation *e = gpr_malloc(sizeof(expectation));
  e->type = type;
  e->tag = tag;
  e->next = &v->expect;
  e->prev = e->next->prev;
  e->next->prev = e->prev->next = e;
  return e;
}

void cq_expect_completion(cq_verifier *v, void *tag, bool success) {
  add(v, GRPC_OP_COMPLETE, tag)->success = success;
}
