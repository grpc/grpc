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
  const char *file;
  int line;
  grpc_completion_type type;
  void *tag;
  int success;
} expectation;

/* the verifier itself */
struct cq_verifier {
  /* bound completion queue */
  grpc_completion_queue *cq;
  /* start of expectation list */
  expectation *first_expectation;
};

cq_verifier *cq_verifier_create(grpc_completion_queue *cq) {
  cq_verifier *v = (cq_verifier *)gpr_malloc(sizeof(cq_verifier));
  v->cq = cq;
  v->first_expectation = NULL;
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
    if (0 == grpc_slice_str_cmp(md[i].key, key) &&
        0 == grpc_slice_str_cmp(md[i].value, value)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata(grpc_metadata_array *array, const char *key,
                      const char *value) {
  return has_metadata(array->metadata, array->count, key, value);
}

static int has_metadata_slices(const grpc_metadata *md, size_t count,
                               grpc_slice key, grpc_slice value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (grpc_slice_eq(md[i].key, key) && grpc_slice_eq(md[i].value, value)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata_slices(grpc_metadata_array *array, grpc_slice key,
                             grpc_slice value) {
  return has_metadata_slices(array->metadata, array->count, key, value);
}

static grpc_slice merge_slices(grpc_slice *slices, size_t nslices) {
  size_t i;
  size_t len = 0;
  uint8_t *cursor;
  grpc_slice out;

  for (i = 0; i < nslices; i++) {
    len += GRPC_SLICE_LENGTH(slices[i]);
  }

  out = grpc_slice_malloc(len);
  cursor = GRPC_SLICE_START_PTR(out);

  for (i = 0; i < nslices; i++) {
    memcpy(cursor, GRPC_SLICE_START_PTR(slices[i]),
           GRPC_SLICE_LENGTH(slices[i]));
    cursor += GRPC_SLICE_LENGTH(slices[i]);
  }

  return out;
}

int raw_byte_buffer_eq_slice(grpc_byte_buffer *rbb, grpc_slice b) {
  grpc_slice a;
  int ok;

  if (!rbb) return 0;

  a = merge_slices(rbb->data.raw.slice_buffer.slices,
                   rbb->data.raw.slice_buffer.count);
  ok = GRPC_SLICE_LENGTH(a) == GRPC_SLICE_LENGTH(b) &&
       0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                   GRPC_SLICE_LENGTH(a));
  grpc_slice_unref(a);
  grpc_slice_unref(b);
  return ok;
}

int byte_buffer_eq_slice(grpc_byte_buffer *bb, grpc_slice b) {
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer *rbb;
  int res;

  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, bb) &&
             "Couldn't init byte buffer reader");
  rbb = grpc_raw_byte_buffer_from_reader(&reader);
  res = raw_byte_buffer_eq_slice(rbb, b);
  grpc_byte_buffer_reader_destroy(&reader);
  grpc_byte_buffer_destroy(rbb);

  return res;
}

int byte_buffer_eq_string(grpc_byte_buffer *bb, const char *str) {
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer *rbb;
  int res;

  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, bb) &&
             "Couldn't init byte buffer reader");
  rbb = grpc_raw_byte_buffer_from_reader(&reader);
  res = raw_byte_buffer_eq_slice(rbb, grpc_slice_from_copied_string(str));
  grpc_byte_buffer_reader_destroy(&reader);
  grpc_byte_buffer_destroy(rbb);

  return res;
}

static bool is_probably_integer(void *p) { return ((uintptr_t)p) < 1000000; }

static void expectation_to_strvec(gpr_strvec *buf, expectation *e) {
  char *tmp;

  if (is_probably_integer(e->tag)) {
    gpr_asprintf(&tmp, "tag(%" PRIdPTR ") ", (intptr_t)e->tag);
  } else {
    gpr_asprintf(&tmp, "%p ", e->tag);
  }
  gpr_strvec_add(buf, tmp);

  switch (e->type) {
    case GRPC_OP_COMPLETE:
      gpr_asprintf(&tmp, "GRPC_OP_COMPLETE success=%d %s:%d", e->success,
                   e->file, e->line);
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

  for (e = v->first_expectation; e != NULL; e = e->next) {
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

static void verify_matches(expectation *e, grpc_event *ev) {
  GPR_ASSERT(e->type == ev->type);
  switch (e->type) {
    case GRPC_OP_COMPLETE:
      if (e->success != ev->success) {
        gpr_strvec expected;
        gpr_strvec_init(&expected);
        expectation_to_strvec(&expected, e);
        char *s = gpr_strvec_flatten(&expected, NULL);
        gpr_strvec_destroy(&expected);
        gpr_log(GPR_ERROR, "actual success does not match expected: %s", s);
        gpr_free(s);
        abort();
      }
      break;
    case GRPC_QUEUE_SHUTDOWN:
      gpr_log(GPR_ERROR, "premature queue shutdown");
      abort();
      break;
    case GRPC_QUEUE_TIMEOUT:
      gpr_log(GPR_ERROR, "not implemented");
      abort();
      break;
  }
}

void cq_verify(cq_verifier *v) {
  const gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
  while (v->first_expectation != NULL) {
    grpc_event ev = grpc_completion_queue_next(v->cq, deadline, NULL);
    if (ev.type == GRPC_QUEUE_TIMEOUT) {
      fail_no_event_received(v);
      break;
    }
    expectation *e;
    expectation *prev = NULL;
    for (e = v->first_expectation; e != NULL; e = e->next) {
      if (e->tag == ev.tag) {
        verify_matches(e, &ev);
        if (e == v->first_expectation) v->first_expectation = e->next;
        if (prev != NULL) prev->next = e->next;
        gpr_free(e);
        break;
      }
      prev = e;
    }
    if (e == NULL) {
      char *s = grpc_event_string(&ev);
      gpr_log(GPR_ERROR, "cq returned unexpected event: %s", s);
      gpr_free(s);
      gpr_strvec expectations;
      gpr_strvec_init(&expectations);
      expectations_to_strvec(&expectations, v);
      s = gpr_strvec_flatten(&expectations, NULL);
      gpr_strvec_destroy(&expectations);
      gpr_log(GPR_ERROR, "expected tags:\n%s", s);
      gpr_free(s);
      abort();
    }
  }
}

void cq_verify_empty_timeout(cq_verifier *v, int timeout_sec) {
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_seconds(timeout_sec, GPR_TIMESPAN));
  grpc_event ev;

  GPR_ASSERT(v->first_expectation == NULL && "expectation queue must be empty");

  ev = grpc_completion_queue_next(v->cq, deadline, NULL);
  if (ev.type != GRPC_QUEUE_TIMEOUT) {
    char *s = grpc_event_string(&ev);
    gpr_log(GPR_ERROR, "unexpected event (expected nothing): %s", s);
    gpr_free(s);
    abort();
  }
}

void cq_verify_empty(cq_verifier *v) { cq_verify_empty_timeout(v, 1); }

static void add(cq_verifier *v, const char *file, int line,
                grpc_completion_type type, void *tag, bool success) {
  expectation *e = (expectation *)gpr_malloc(sizeof(expectation));
  e->type = type;
  e->file = file;
  e->line = line;
  e->tag = tag;
  e->success = success;
  e->next = v->first_expectation;
  v->first_expectation = e;
}

void cq_expect_completion(cq_verifier *v, const char *file, int line, void *tag,
                          bool success) {
  add(v, file, line, GRPC_OP_COMPLETE, tag, success);
}
