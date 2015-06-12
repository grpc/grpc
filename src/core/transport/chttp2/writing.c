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

static void grpc_chttp2_unlocking_check_writes(grpc_chttp2_transport *t) {
  grpc_chttp2_stream *s;
  gpr_uint32 window_delta;

  /* don't do anything if we are already writing */
  if (t->writing.executing) {
    return;
  }

  /* simple writes are queued to qbuf, and flushed here */
  gpr_slice_buffer_swap(&t->global.qbuf, &t->writing.outbuf);
  GPR_ASSERT(t->global.qbuf.count == 0);

  if (t->dirtied_local_settings && !t->sent_local_settings) {
    gpr_slice_buffer_add(
        &t->writing.outbuf, grpc_chttp2_settings_create(
                        t->settings[SENT_SETTINGS], t->settings[LOCAL_SETTINGS],
                        t->force_send_settings, GRPC_CHTTP2_NUM_SETTINGS));
    t->force_send_settings = 0;
    t->dirtied_local_settings = 0;
    t->sent_local_settings = 1;
  }

  /* for each grpc_chttp2_stream that's become writable, frame it's data (according to
     available window sizes) and add to the output buffer */
  while (t->outgoing_window && (s = stream_list_remove_head(t, WRITABLE)) &&
         s->outgoing_window > 0) {
    window_delta = grpc_chttp2_preencode(
        s->outgoing_sopb->ops, &s->outgoing_sopb->nops,
        GPR_MIN(t->outgoing_window, s->outgoing_window), &s->writing.sopb);
    FLOWCTL_TRACE(t, t, outgoing, 0, -(gpr_int64)window_delta);
    FLOWCTL_TRACE(t, s, outgoing, s->id, -(gpr_int64)window_delta);
    t->outgoing_window -= window_delta;
    s->outgoing_window -= window_delta;

    if (s->write_state == WRITE_STATE_QUEUED_CLOSE &&
        s->outgoing_sopb->nops == 0) {
      if (!t->is_client && !s->read_closed) {
        s->writing.send_closed = SEND_CLOSED_WITH_RST_STREAM;
      } else {
        s->writing.send_closed = SEND_CLOSED;
      }
    }
    if (s->writing.sopb.nops > 0 || s->writing.send_closed) {
      stream_list_join(t, s, WRITING);
    }

    /* we should either exhaust window or have no ops left, but not both */
    if (s->outgoing_sopb->nops == 0) {
      s->outgoing_sopb = NULL;
      schedule_cb(t, s->global.send_done_closure, 1);
    } else if (s->outgoing_window) {
      stream_list_add_tail(t, s, WRITABLE);
    }
  }

  if (!t->parsing.executing) {
    /* for each grpc_chttp2_stream that wants to update its window, add that window here */
    while ((s = stream_list_remove_head(t, WINDOW_UPDATE))) {
      window_delta =
          t->settings[LOCAL_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] -
          s->incoming_window;
      if (!s->read_closed && window_delta) {
        gpr_slice_buffer_add(
            &t->writing.outbuf, grpc_chttp2_window_update_create(s->id, window_delta));
        FLOWCTL_TRACE(t, s, incoming, s->id, window_delta);
        s->incoming_window += window_delta;
      }
    }

    /* if the grpc_chttp2_transport is ready to send a window update, do so here also */
    if (t->incoming_window < t->connection_window_target * 3 / 4) {
      window_delta = t->connection_window_target - t->incoming_window;
      gpr_slice_buffer_add(&t->writing.outbuf,
                           grpc_chttp2_window_update_create(0, window_delta));
      FLOWCTL_TRACE(t, t, incoming, 0, window_delta);
      t->incoming_window += window_delta;
    }
  }

  if (t->writing.outbuf.length > 0 || !stream_list_empty(t, WRITING)) {
    t->writing.executing = 1;
    ref_transport(t);
    gpr_log(GPR_DEBUG, "schedule write");
    schedule_cb(t, &t->writing.action, 1);
  }
}
