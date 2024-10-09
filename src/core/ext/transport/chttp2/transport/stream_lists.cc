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

#include "src/core/ext/transport/chttp2/transport/stream_lists.h"

#include <grpc/support/port_platform.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/bitset.h"

static const char* stream_list_id_string(grpc_chttp2_stream_list_id id) {
  switch (id) {
    case GRPC_CHTTP2_LIST_WRITABLE:
      return "writable";
    case GRPC_CHTTP2_LIST_WRITING:
      return "writing";
    case GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT:
      return "stalled_by_transport";
    case GRPC_CHTTP2_LIST_STALLED_BY_STREAM:
      return "stalled_by_stream";
    case GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY:
      return "waiting_for_concurrency";
    case STREAM_LIST_COUNT:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

// core list management

static bool stream_list_empty(grpc_chttp2_transport* t,
                              grpc_chttp2_stream_list_id id) {
  return t->lists[id].head == nullptr;
}

static bool stream_list_pop(grpc_chttp2_transport* t,
                            grpc_chttp2_stream** stream,
                            grpc_chttp2_stream_list_id id) {
  grpc_chttp2_stream* s = t->lists[id].head;
  if (s) {
    grpc_chttp2_stream* new_head = s->links[id].next;
    CHECK(s->included.is_set(id));
    if (new_head) {
      t->lists[id].head = new_head;
      new_head->links[id].prev = nullptr;
    } else {
      t->lists[id].head = nullptr;
      t->lists[id].tail = nullptr;
    }
    s->included.clear(id);
  }
  *stream = s;
  if (s && GRPC_TRACE_FLAG_ENABLED(http2_stream_state)) {
    LOG(INFO) << t << "[" << s->id << "][" << (t->is_client ? "cli" : "svr")
              << "]: pop from " << stream_list_id_string(id);
  }
  return s != nullptr;
}

static void stream_list_remove(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                               grpc_chttp2_stream_list_id id) {
  CHECK(s->included.is_set(id));
  s->included.clear(id);
  if (s->links[id].prev) {
    s->links[id].prev->links[id].next = s->links[id].next;
  } else {
    CHECK(t->lists[id].head == s);
    t->lists[id].head = s->links[id].next;
  }
  if (s->links[id].next) {
    s->links[id].next->links[id].prev = s->links[id].prev;
  } else {
    t->lists[id].tail = s->links[id].prev;
  }
  GRPC_TRACE_LOG(http2_stream_state, INFO)
      << t << "[" << s->id << "][" << (t->is_client ? "cli" : "svr")
      << "]: remove from " << stream_list_id_string(id);
}

static bool stream_list_maybe_remove(grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s,
                                     grpc_chttp2_stream_list_id id) {
  if (s->included.is_set(id)) {
    stream_list_remove(t, s, id);
    return true;
  } else {
    return false;
  }
}

static void stream_list_add_tail(grpc_chttp2_transport* t,
                                 grpc_chttp2_stream* s,
                                 grpc_chttp2_stream_list_id id) {
  grpc_chttp2_stream* old_tail;
  CHECK(!s->included.is_set(id));
  old_tail = t->lists[id].tail;
  s->links[id].next = nullptr;
  s->links[id].prev = old_tail;
  if (old_tail) {
    old_tail->links[id].next = s;
  } else {
    t->lists[id].head = s;
  }
  t->lists[id].tail = s;
  s->included.set(id);
  GRPC_TRACE_LOG(http2_stream_state, INFO)
      << t << "[" << s->id << "][" << (t->is_client ? "cli" : "svr")
      << "]: add to " << stream_list_id_string(id);
}

static bool stream_list_add(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                            grpc_chttp2_stream_list_id id) {
  if (s->included.is_set(id)) {
    return false;
  }
  stream_list_add_tail(t, s, id);
  return true;
}

// wrappers for specializations

bool grpc_chttp2_list_add_writable_stream(grpc_chttp2_transport* t,
                                          grpc_chttp2_stream* s) {
  CHECK_NE(s->id, 0u);
  return stream_list_add(t, s, GRPC_CHTTP2_LIST_WRITABLE);
}

bool grpc_chttp2_list_pop_writable_stream(grpc_chttp2_transport* t,
                                          grpc_chttp2_stream** s) {
  return stream_list_pop(t, s, GRPC_CHTTP2_LIST_WRITABLE);
}

bool grpc_chttp2_list_remove_writable_stream(grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s) {
  return stream_list_maybe_remove(t, s, GRPC_CHTTP2_LIST_WRITABLE);
}

bool grpc_chttp2_list_add_writing_stream(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream* s) {
  return stream_list_add(t, s, GRPC_CHTTP2_LIST_WRITING);
}

bool grpc_chttp2_list_have_writing_streams(grpc_chttp2_transport* t) {
  return !stream_list_empty(t, GRPC_CHTTP2_LIST_WRITING);
}

bool grpc_chttp2_list_pop_writing_stream(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream** s) {
  return stream_list_pop(t, s, GRPC_CHTTP2_LIST_WRITING);
}

void grpc_chttp2_list_add_waiting_for_concurrency(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s) {
  stream_list_add(t, s, GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY);
}

bool grpc_chttp2_list_pop_waiting_for_concurrency(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream** s) {
  return stream_list_pop(t, s, GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY);
}

void grpc_chttp2_list_remove_waiting_for_concurrency(grpc_chttp2_transport* t,
                                                     grpc_chttp2_stream* s) {
  stream_list_maybe_remove(t, s, GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY);
}

void grpc_chttp2_list_add_stalled_by_transport(grpc_chttp2_transport* t,
                                               grpc_chttp2_stream* s) {
  stream_list_add(t, s, GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT);
}

bool grpc_chttp2_list_pop_stalled_by_transport(grpc_chttp2_transport* t,
                                               grpc_chttp2_stream** s) {
  return stream_list_pop(t, s, GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT);
}

void grpc_chttp2_list_remove_stalled_by_transport(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s) {
  stream_list_maybe_remove(t, s, GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT);
}

void grpc_chttp2_list_add_stalled_by_stream(grpc_chttp2_transport* t,
                                            grpc_chttp2_stream* s) {
  stream_list_add(t, s, GRPC_CHTTP2_LIST_STALLED_BY_STREAM);
}

bool grpc_chttp2_list_pop_stalled_by_stream(grpc_chttp2_transport* t,
                                            grpc_chttp2_stream** s) {
  return stream_list_pop(t, s, GRPC_CHTTP2_LIST_STALLED_BY_STREAM);
}

bool grpc_chttp2_list_remove_stalled_by_stream(grpc_chttp2_transport* t,
                                               grpc_chttp2_stream* s) {
  return stream_list_maybe_remove(t, s, GRPC_CHTTP2_LIST_STALLED_BY_STREAM);
}
