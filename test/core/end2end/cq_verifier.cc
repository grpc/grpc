//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/core/end2end/cq_verifier.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <list>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/surface/event_string.h"

#define ROOT_EXPECTATION 1000

// a set of metadata we expect to find on an event
typedef struct metadata {
  size_t count;
  size_t cap;
  char** keys;
  char** values;
} metadata;

// details what we expect to find on a single event
struct Expectation {
  Expectation(const char* f, int l, grpc_completion_type t, void* tag_arg,
              bool check_success_arg, int success_arg, bool* seen_arg)
      : file(f),
        line(l),
        type(t),
        tag(tag_arg),
        check_success(check_success_arg),
        success(success_arg),
        seen(seen_arg) {}
  const char* file;
  int line;
  grpc_completion_type type;
  void* tag;
  bool check_success;
  int success;
  bool* seen;
};

// the verifier itself
struct cq_verifier {
  // bound completion queue
  grpc_completion_queue* cq;
  // expectation list
  std::list<Expectation> expectations;
  // maybe expectation list
  std::list<Expectation> maybe_expectations;
};

// TODO(yashykt): Convert this to constructor/destructor pair
cq_verifier* cq_verifier_create(grpc_completion_queue* cq) {
  cq_verifier* v = new cq_verifier;
  v->cq = cq;
  return v;
}

void cq_verifier_destroy(cq_verifier* v) {
  cq_verify(v);
  delete v;
}

static int has_metadata(const grpc_metadata* md, size_t count, const char* key,
                        const char* value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (0 == grpc_slice_str_cmp(md[i].key, key) &&
        0 == grpc_slice_str_cmp(md[i].value, value)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata(grpc_metadata_array* array, const char* key,
                      const char* value) {
  return has_metadata(array->metadata, array->count, key, value);
}

static int has_metadata_slices(const grpc_metadata* md, size_t count,
                               grpc_slice key, grpc_slice value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (grpc_slice_eq(md[i].key, key) && grpc_slice_eq(md[i].value, value)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata_slices(grpc_metadata_array* array, grpc_slice key,
                             grpc_slice value) {
  return has_metadata_slices(array->metadata, array->count, key, value);
}

static grpc_slice merge_slices(grpc_slice* slices, size_t nslices) {
  size_t i;
  size_t len = 0;
  uint8_t* cursor;
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

int raw_byte_buffer_eq_slice(grpc_byte_buffer* rbb, grpc_slice b) {
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

int byte_buffer_eq_slice(grpc_byte_buffer* bb, grpc_slice b) {
  if (bb->data.raw.compression > GRPC_COMPRESS_NONE) {
    grpc_slice_buffer decompressed_buffer;
    grpc_slice_buffer_init(&decompressed_buffer);
    GPR_ASSERT(grpc_msg_decompress(
        grpc_compression_algorithm_to_message_compression_algorithm(
            bb->data.raw.compression),
        &bb->data.raw.slice_buffer, &decompressed_buffer));
    grpc_byte_buffer* rbb = grpc_raw_byte_buffer_create(
        decompressed_buffer.slices, decompressed_buffer.count);
    int ret_val = raw_byte_buffer_eq_slice(rbb, b);
    grpc_byte_buffer_destroy(rbb);
    grpc_slice_buffer_destroy(&decompressed_buffer);
    return ret_val;
  }
  return raw_byte_buffer_eq_slice(bb, b);
}

int byte_buffer_eq_string(grpc_byte_buffer* bb, const char* str) {
  return byte_buffer_eq_slice(bb, grpc_slice_from_copied_string(str));
}

static bool is_probably_integer(void* p) {
  return reinterpret_cast<uintptr_t>(p) < 1000000;
}

namespace {

std::string ExpectationString(const Expectation& e) {
  std::string out;
  if (is_probably_integer(e.tag)) {
    out = absl::StrFormat("tag(%" PRIdPTR ") ",
                          reinterpret_cast<intptr_t>(e.tag));
  } else {
    out = absl::StrFormat("%p ", e.tag);
  }
  switch (e.type) {
    case GRPC_OP_COMPLETE:
      absl::StrAppendFormat(&out, "GRPC_OP_COMPLETE success=%d %s:%d",
                            e.success, e.file, e.line);
      break;
    case GRPC_QUEUE_TIMEOUT:
    case GRPC_QUEUE_SHUTDOWN:
      gpr_log(GPR_ERROR, "not implemented");
      abort();
      break;
  }
  return out;
}

std::string ExpectationsString(const cq_verifier& v) {
  std::vector<std::string> expectations;
  for (const auto& e : v.expectations) {
    expectations.push_back(ExpectationString(e));
  }
  return absl::StrJoin(expectations, "\n");
}

}  // namespace

static void fail_no_event_received(cq_verifier* v) {
  gpr_log(GPR_ERROR, "no event received, but expected:%s",
          ExpectationsString(*v).c_str());
  abort();
}

static void verify_matches(const Expectation& e, const grpc_event& ev) {
  GPR_ASSERT(e.type == ev.type);
  switch (e.type) {
    case GRPC_OP_COMPLETE:
      if (e.check_success && e.success != ev.success) {
        gpr_log(GPR_ERROR, "actual success does not match expected: %s",
                ExpectationString(e).c_str());
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

// Try to find the event in the expectations list
bool FindExpectations(std::list<Expectation>* expectations,
                      const grpc_event& ev) {
  for (auto e = expectations->begin(); e != expectations->end(); ++e) {
    if (e->tag == ev.tag) {
      verify_matches(*e, ev);
      if (e->seen != nullptr) {
        *(e->seen) = true;
      }
      expectations->erase(e);
      return true;
    }
  }
  return false;
}

void cq_verify(cq_verifier* v, int timeout_sec) {
  const gpr_timespec deadline = grpc_timeout_seconds_to_deadline(timeout_sec);
  while (!v->expectations.empty()) {
    grpc_event ev = grpc_completion_queue_next(v->cq, deadline, nullptr);
    if (ev.type == GRPC_QUEUE_TIMEOUT) {
      fail_no_event_received(v);
      break;
    }
    if (FindExpectations(&v->expectations, ev)) continue;
    if (FindExpectations(&v->maybe_expectations, ev)) continue;
    gpr_log(GPR_ERROR, "cq returned unexpected event: %s",
            grpc_event_string(&ev).c_str());
    gpr_log(GPR_ERROR, "expected tags:\n%s", ExpectationsString(*v).c_str());
    abort();
  }
  v->maybe_expectations.clear();
}

void cq_verify_empty_timeout(cq_verifier* v, int timeout_sec) {
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_seconds(timeout_sec, GPR_TIMESPAN));
  grpc_event ev;

  GPR_ASSERT(v->expectations.empty() && "expectation queue must be empty");

  ev = grpc_completion_queue_next(v->cq, deadline, nullptr);
  if (ev.type != GRPC_QUEUE_TIMEOUT) {
    gpr_log(GPR_ERROR, "unexpected event (expected nothing): %s",
            grpc_event_string(&ev).c_str());
    abort();
  }
}

void cq_verify_empty(cq_verifier* v) { cq_verify_empty_timeout(v, 1); }

void cq_maybe_expect_completion(cq_verifier* v, const char* file, int line,
                                void* tag, bool success, bool* seen) {
  v->maybe_expectations.emplace_back(file, line, GRPC_OP_COMPLETE, tag,
                                     true /* check_success */, success, seen);
}

static void add(cq_verifier* v, const char* file, int line,
                grpc_completion_type type, void* tag, bool check_success,
                bool success) {
  v->expectations.emplace_back(file, line, type, tag, check_success, success,
                               nullptr);
}

void cq_expect_completion(cq_verifier* v, const char* file, int line, void* tag,
                          bool success) {
  add(v, file, line, GRPC_OP_COMPLETE, tag, true, success);
}

void cq_expect_completion_any_status(cq_verifier* v, const char* file, int line,
                                     void* tag) {
  add(v, file, line, GRPC_OP_COMPLETE, tag, false, false);
}
