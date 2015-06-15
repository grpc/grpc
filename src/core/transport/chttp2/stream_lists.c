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

/* core list management */

static int stream_list_empty(grpc_chttp2_transport *t,
                             grpc_chttp2_stream_list_id id) {
  return t->lists[id].head == NULL;
}

static grpc_chttp2_stream *stream_list_remove_head(
    grpc_chttp2_transport *t, grpc_chttp2_stream_list_id id) {
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
  return s;
}

static void stream_list_remove(grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                               grpc_chttp2_stream_list_id id) {
  if (!s->included[id]) return;
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

static void stream_list_join(grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                             grpc_chttp2_stream_list_id id) {
  if (s->included[id]) {
    return;
  }
  stream_list_add_tail(t, s, id);
}

