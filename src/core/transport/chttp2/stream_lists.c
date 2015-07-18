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

#include "src/core/transport/chttp2/internal.h"

#include <grpc/support/log.h>

#define TRANSPORT_FROM_GLOBAL(tg)                                         \
  ((grpc_chttp2_transport *)((char *)(tg)-offsetof(grpc_chttp2_transport, \
                                                   global)))

#define STREAM_FROM_GLOBAL(sg) \
  ((grpc_chttp2_stream *)((char *)(sg)-offsetof(grpc_chttp2_stream, global)))

#define TRANSPORT_FROM_WRITING(tw)                                        \
  ((grpc_chttp2_transport *)((char *)(tw)-offsetof(grpc_chttp2_transport, \
                                                   writing)))

#define STREAM_FROM_WRITING(sw) \
  ((grpc_chttp2_stream *)((char *)(sw)-offsetof(grpc_chttp2_stream, writing)))

#define TRANSPORT_FROM_PARSING(tp)                                        \
  ((grpc_chttp2_transport *)((char *)(tp)-offsetof(grpc_chttp2_transport, \
                                                   parsing)))

#define STREAM_FROM_PARSING(sp) \
  ((grpc_chttp2_stream *)((char *)(sp)-offsetof(grpc_chttp2_stream, parsing)))

/* core list management */

static int stream_list_empty(grpc_chttp2_transport *t,
                             grpc_chttp2_stream_list_id id) {
  return t->lists[id].head == NULL;
}

static int stream_list_pop(grpc_chttp2_transport *t,
                           grpc_chttp2_stream **stream,
                           grpc_chttp2_stream_list_id id) {
  grpc_chttp2_stream *s = t->lists[id].head;
  if (s) {
    grpc_chttp2_stream *new_head = s->links[id].next;
    GPR_ASSERT(s->included[id]);
    if (new_head) {
      t->lists[id].head = new_head;
      new_head->links[id].prev = NULL;
    } else {
      t->lists[id].head = NULL;
      t->lists[id].tail = NULL;
    }
    s->included[id] = 0;
  }
  *stream = s;
  return s != 0;
}

static void stream_list_remove(grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                               grpc_chttp2_stream_list_id id) {
  GPR_ASSERT(s->included[id]);
  s->included[id] = 0;
  if (s->links[id].prev) {
    s->links[id].prev->links[id].next = s->links[id].next;
  } else {
    GPR_ASSERT(t->lists[id].head == s);
    t->lists[id].head = s->links[id].next;
  }
  if (s->links[id].next) {
    s->links[id].next->links[id].prev = s->links[id].prev;
  } else {
    t->lists[id].tail = s->links[id].prev;
  }
}

static void stream_list_maybe_remove(grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s,
                                     grpc_chttp2_stream_list_id id) {
  if (s->included[id]) {
    stream_list_remove(t, s, id);
  }
}

static void stream_list_add_tail(grpc_chttp2_transport *t,
                                 grpc_chttp2_stream *s,
                                 grpc_chttp2_stream_list_id id) {
  grpc_chttp2_stream *old_tail;
  GPR_ASSERT(!s->included[id]);
  old_tail = t->lists[id].tail;
  s->links[id].next = NULL;
  s->links[id].prev = old_tail;
  if (old_tail) {
    old_tail->links[id].next = s;
  } else {
    s->links[id].prev = NULL;
    t->lists[id].head = s;
  }
  t->lists[id].tail = s;
  s->included[id] = 1;
}

static void stream_list_add(grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                            grpc_chttp2_stream_list_id id) {
  if (s->included[id]) {
    return;
  }
  stream_list_add_tail(t, s, id);
}

/* wrappers for specializations */

void grpc_chttp2_list_add_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  GPR_ASSERT(stream_global->id != 0);
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global), GRPC_CHTTP2_LIST_WRITABLE);
}

int grpc_chttp2_list_pop_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_writing **stream_writing) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_WRITABLE);
  *stream_global = &stream->global;
  *stream_writing = &stream->writing;
  return r;
}

void grpc_chttp2_list_add_writing_stream(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing *stream_writing) {
  stream_list_add(TRANSPORT_FROM_WRITING(transport_writing),
                  STREAM_FROM_WRITING(stream_writing),
                  GRPC_CHTTP2_LIST_WRITING);
}

int grpc_chttp2_list_have_writing_streams(
    grpc_chttp2_transport_writing *transport_writing) {
  return !stream_list_empty(TRANSPORT_FROM_WRITING(transport_writing),
                            GRPC_CHTTP2_LIST_WRITING);
}

int grpc_chttp2_list_pop_writing_stream(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing **stream_writing) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_WRITING(transport_writing), &stream,
                          GRPC_CHTTP2_LIST_WRITING);
  *stream_writing = &stream->writing;
  return r;
}

void grpc_chttp2_list_add_written_stream(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing *stream_writing) {
  stream_list_add(TRANSPORT_FROM_WRITING(transport_writing),
                  STREAM_FROM_WRITING(stream_writing),
                  GRPC_CHTTP2_LIST_WRITTEN);
}

int grpc_chttp2_list_pop_written_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_writing **stream_writing) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_WRITING(transport_writing), &stream,
                          GRPC_CHTTP2_LIST_WRITTEN);
  *stream_global = &stream->global;
  *stream_writing = &stream->writing;
  return r;
}

void grpc_chttp2_list_add_writable_window_update_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  GPR_ASSERT(stream_global->id != 0);
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global),
                  GRPC_CHTTP2_LIST_WRITABLE_WINDOW_UPDATE);
}

int grpc_chttp2_list_pop_writable_window_update_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_writing **stream_writing) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_WRITABLE_WINDOW_UPDATE);
  *stream_global = &stream->global;
  *stream_writing = &stream->writing;
  return r;
}

void grpc_chttp2_list_remove_writable_window_update_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_maybe_remove(TRANSPORT_FROM_GLOBAL(transport_global),
                           STREAM_FROM_GLOBAL(stream_global),
                           GRPC_CHTTP2_LIST_WRITABLE_WINDOW_UPDATE);
}

void grpc_chttp2_list_add_parsing_seen_stream(
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing) {
  stream_list_add(TRANSPORT_FROM_PARSING(transport_parsing),
                  STREAM_FROM_PARSING(stream_parsing),
                  GRPC_CHTTP2_LIST_PARSING_SEEN);
}

int grpc_chttp2_list_pop_parsing_seen_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_parsing **stream_parsing) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_PARSING(transport_parsing), &stream,
                          GRPC_CHTTP2_LIST_PARSING_SEEN);
  *stream_global = &stream->global;
  *stream_parsing = &stream->parsing;
  return r;
}

void grpc_chttp2_list_add_waiting_for_concurrency(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global),
                  GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY);
}

int grpc_chttp2_list_pop_waiting_for_concurrency(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY);
  *stream_global = &stream->global;
  return r;
}

void grpc_chttp2_list_add_closed_waiting_for_parsing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global),
                  GRPC_CHTTP2_LIST_CLOSED_WAITING_FOR_PARSING);
}

int grpc_chttp2_list_pop_closed_waiting_for_parsing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_CLOSED_WAITING_FOR_PARSING);
  *stream_global = &stream->global;
  return r;
}

void grpc_chttp2_list_add_cancelled_waiting_for_writing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global),
                  GRPC_CHTTP2_LIST_CANCELLED_WAITING_FOR_WRITING);
}

int grpc_chttp2_list_pop_cancelled_waiting_for_writing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_CANCELLED_WAITING_FOR_WRITING);
  *stream_global = &stream->global;
  return r;
}

void grpc_chttp2_list_add_incoming_window_updated(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global),
                  GRPC_CHTTP2_LIST_INCOMING_WINDOW_UPDATED);
}

int grpc_chttp2_list_pop_incoming_window_updated(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_parsing **stream_parsing) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_INCOMING_WINDOW_UPDATED);
  *stream_global = &stream->global;
  *stream_parsing = &stream->parsing;
  return r;
}

void grpc_chttp2_list_remove_incoming_window_updated(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_maybe_remove(TRANSPORT_FROM_GLOBAL(transport_global),
                           STREAM_FROM_GLOBAL(stream_global),
                           GRPC_CHTTP2_LIST_INCOMING_WINDOW_UPDATED);
}

void grpc_chttp2_list_add_read_write_state_changed(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  stream_list_add(TRANSPORT_FROM_GLOBAL(transport_global),
                  STREAM_FROM_GLOBAL(stream_global),
                  GRPC_CHTTP2_LIST_READ_WRITE_STATE_CHANGED);
}

int grpc_chttp2_list_pop_read_write_state_changed(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global) {
  grpc_chttp2_stream *stream;
  int r = stream_list_pop(TRANSPORT_FROM_GLOBAL(transport_global), &stream,
                          GRPC_CHTTP2_LIST_READ_WRITE_STATE_CHANGED);
  *stream_global = &stream->global;
  return r;
}

void grpc_chttp2_register_stream(grpc_chttp2_transport *t,
                                 grpc_chttp2_stream *s) {
  stream_list_add_tail(t, s, GRPC_CHTTP2_LIST_ALL_STREAMS);
}

int grpc_chttp2_unregister_stream(grpc_chttp2_transport *t,
                                   grpc_chttp2_stream *s) {
  stream_list_maybe_remove(t, s, GRPC_CHTTP2_LIST_ALL_STREAMS);
  return stream_list_empty(t, GRPC_CHTTP2_LIST_ALL_STREAMS);
}

int grpc_chttp2_has_streams(grpc_chttp2_transport *t) {
  return !stream_list_empty(t, GRPC_CHTTP2_LIST_ALL_STREAMS);
}

void grpc_chttp2_for_all_streams(
    grpc_chttp2_transport_global *transport_global, void *user_data,
    void (*cb)(grpc_chttp2_transport_global *transport_global, void *user_data,
               grpc_chttp2_stream_global *stream_global)) {
  grpc_chttp2_stream *s;
  grpc_chttp2_transport *t = TRANSPORT_FROM_GLOBAL(transport_global);
  for (s = t->lists[GRPC_CHTTP2_LIST_ALL_STREAMS].head; s != NULL;
       s = s->links[GRPC_CHTTP2_LIST_ALL_STREAMS].next) {
    cb(transport_global, user_data, &s->global);
  }
}
