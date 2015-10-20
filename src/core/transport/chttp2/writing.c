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

#include <limits.h>

#include <grpc/support/log.h>

#include "src/core/profiling/timers.h"
#include "src/core/transport/chttp2/http2_errors.h"

static void finalize_outbuf(grpc_chttp2_transport_writing *transport_writing);

int grpc_chttp2_unlocking_check_writes(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing) {
  grpc_chttp2_stream_global *stream_global;
  grpc_chttp2_stream_writing *stream_writing;
  grpc_chttp2_stream_global *first_reinserted_stream = NULL;
  gpr_uint32 window_delta;

  /* simple writes are queued to qbuf, and flushed here */
  gpr_slice_buffer_swap(&transport_global->qbuf, &transport_writing->outbuf);
  GPR_ASSERT(transport_global->qbuf.count == 0);

  if (transport_global->dirtied_local_settings &&
      !transport_global->sent_local_settings) {
    gpr_slice_buffer_add(
        &transport_writing->outbuf,
        grpc_chttp2_settings_create(
            transport_global->settings[GRPC_SENT_SETTINGS],
            transport_global->settings[GRPC_LOCAL_SETTINGS],
            transport_global->force_send_settings, GRPC_CHTTP2_NUM_SETTINGS));
    transport_global->force_send_settings = 0;
    transport_global->dirtied_local_settings = 0;
    transport_global->sent_local_settings = 1;
  }

  /* for each grpc_chttp2_stream that's become writable, frame it's data
     (according to available window sizes) and add to the output buffer */
  while (grpc_chttp2_list_pop_writable_stream(
      transport_global, transport_writing, &stream_global, &stream_writing)) {
    if (stream_global == first_reinserted_stream) {
      /* prevent infinite loop */
      grpc_chttp2_list_add_first_writable_stream(transport_global,
                                                 stream_global);
      break;
    }

    stream_writing->id = stream_global->id;
    stream_writing->send_closed = GRPC_DONT_SEND_CLOSED;

    if (stream_global->outgoing_sopb) {
      window_delta = grpc_chttp2_preencode(
          stream_global->outgoing_sopb->ops,
          &stream_global->outgoing_sopb->nops,
          (gpr_uint32)GPR_MIN(GPR_MIN(transport_global->outgoing_window,
                                      stream_global->outgoing_window),
                              GPR_UINT32_MAX),
          &stream_writing->sopb);
      GRPC_CHTTP2_FLOWCTL_TRACE_TRANSPORT(
          "write", transport_global, outgoing_window, -(gpr_int64)window_delta);
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM("write", transport_global, stream_global,
                                       outgoing_window,
                                       -(gpr_int64)window_delta);
      transport_global->outgoing_window -= window_delta;
      stream_global->outgoing_window -= window_delta;

      if (stream_global->write_state == GRPC_WRITE_STATE_QUEUED_CLOSE &&
          stream_global->outgoing_sopb->nops == 0) {
        if (!transport_global->is_client && !stream_global->read_closed) {
          stream_writing->send_closed = GRPC_SEND_CLOSED_WITH_RST_STREAM;
        } else {
          stream_writing->send_closed = GRPC_SEND_CLOSED;
        }
      }

      if (stream_global->outgoing_window > 0 &&
          stream_global->outgoing_sopb->nops != 0) {
        grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
        if (first_reinserted_stream == NULL &&
            transport_global->outgoing_window == 0) {
          first_reinserted_stream = stream_global;
        }
      }
    }

    if (!stream_global->read_closed &&
        stream_global->unannounced_incoming_window > 0) {
      GPR_ASSERT(stream_writing->announce_window == 0);
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(
          "write", transport_writing, stream_writing, announce_window,
          stream_global->unannounced_incoming_window);
      stream_writing->announce_window =
          stream_global->unannounced_incoming_window;
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(
          "write", transport_global, stream_global, incoming_window,
          stream_global->unannounced_incoming_window);
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(
          "write", transport_global, stream_global, unannounced_incoming_window,
          -(gpr_int64)stream_global->unannounced_incoming_window);
      stream_global->incoming_window +=
          stream_global->unannounced_incoming_window;
      stream_global->unannounced_incoming_window = 0;
      grpc_chttp2_list_add_incoming_window_updated(transport_global,
                                                   stream_global);
      stream_global->writing_now |= GRPC_CHTTP2_WRITING_WINDOW;
    }
    if (stream_writing->sopb.nops > 0 ||
        stream_writing->send_closed != GRPC_DONT_SEND_CLOSED) {
      stream_global->writing_now |= GRPC_CHTTP2_WRITING_DATA;
    }
    if (stream_global->writing_now != 0) {
      grpc_chttp2_list_add_writing_stream(transport_writing, stream_writing);
    }
  }

  /* if the grpc_chttp2_transport is ready to send a window update, do so here
     also; 3/4 is a magic number that will likely get tuned soon */
  if (transport_global->incoming_window <
      transport_global->connection_window_target * 3 / 4) {
    window_delta = transport_global->connection_window_target -
                   transport_global->incoming_window;
    gpr_slice_buffer_add(&transport_writing->outbuf,
                         grpc_chttp2_window_update_create(0, window_delta));
    GRPC_CHTTP2_FLOWCTL_TRACE_TRANSPORT("write", transport_global,
                                        incoming_window, window_delta);
    transport_global->incoming_window += window_delta;
  }

  return transport_writing->outbuf.count > 0 ||
         grpc_chttp2_list_have_writing_streams(transport_writing);
}

void grpc_chttp2_perform_writes(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_writing *transport_writing,
    grpc_endpoint *endpoint) {
  GPR_ASSERT(transport_writing->outbuf.count > 0 ||
             grpc_chttp2_list_have_writing_streams(transport_writing));

  finalize_outbuf(transport_writing);

  GPR_ASSERT(transport_writing->outbuf.count > 0);
  GPR_ASSERT(endpoint);

  grpc_endpoint_write(exec_ctx, endpoint, &transport_writing->outbuf,
                      &transport_writing->done_cb);
}

static void finalize_outbuf(grpc_chttp2_transport_writing *transport_writing) {
  grpc_chttp2_stream_writing *stream_writing;

  GPR_TIMER_BEGIN("finalize_outbuf", 0);

  while (
      grpc_chttp2_list_pop_writing_stream(transport_writing, &stream_writing)) {
    if (stream_writing->sopb.nops > 0 ||
        stream_writing->send_closed != GRPC_DONT_SEND_CLOSED) {
      grpc_chttp2_encode(stream_writing->sopb.ops, stream_writing->sopb.nops,
                         stream_writing->send_closed != GRPC_DONT_SEND_CLOSED,
                         stream_writing->id,
                         &transport_writing->hpack_compressor,
                         &transport_writing->outbuf);
      stream_writing->sopb.nops = 0;
    }
    if (stream_writing->announce_window > 0) {
      gpr_slice_buffer_add(
          &transport_writing->outbuf,
          grpc_chttp2_window_update_create(stream_writing->id,
                                           stream_writing->announce_window));
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(
          "write", transport_writing, stream_writing, announce_window,
          -(gpr_int64)stream_writing->announce_window);
      stream_writing->announce_window = 0;
    }
    if (stream_writing->send_closed == GRPC_SEND_CLOSED_WITH_RST_STREAM) {
      gpr_slice_buffer_add(&transport_writing->outbuf,
                           grpc_chttp2_rst_stream_create(stream_writing->id,
                                                         GRPC_CHTTP2_NO_ERROR));
    }
    grpc_chttp2_list_add_written_stream(transport_writing, stream_writing);
  }

  GPR_TIMER_END("finalize_outbuf", 0);
}

void grpc_chttp2_cleanup_writing(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing) {
  grpc_chttp2_stream_writing *stream_writing;
  grpc_chttp2_stream_global *stream_global;

  while (grpc_chttp2_list_pop_written_stream(
      transport_global, transport_writing, &stream_global, &stream_writing)) {
    GPR_ASSERT(stream_global->writing_now != 0);
    if (stream_writing->send_closed != GRPC_DONT_SEND_CLOSED) {
      stream_global->write_state = GRPC_WRITE_STATE_SENT_CLOSE;
      if (!transport_global->is_client) {
        stream_global->read_closed = 1;
      }
    }
    if (stream_global->writing_now & GRPC_CHTTP2_WRITING_DATA) {
      if (stream_global->outgoing_sopb != NULL &&
          stream_global->outgoing_sopb->nops == 0) {
        GPR_ASSERT(stream_global->write_state != GRPC_WRITE_STATE_QUEUED_CLOSE);
        stream_global->outgoing_sopb = NULL;
        grpc_exec_ctx_enqueue(exec_ctx, stream_global->send_done_closure, 1);
      }
    }
    stream_global->writing_now = 0;
    grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                  stream_global);
  }
  gpr_slice_buffer_reset_and_unref(&transport_writing->outbuf);
}
