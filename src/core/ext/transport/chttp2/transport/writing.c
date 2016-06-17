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

#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <limits.h>

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/http2_errors.h"
#include "src/core/lib/profiling/timers.h"

static void finalize_outbuf(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_writing *transport_writing);

int grpc_chttp2_unlocking_check_writes(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing, int is_parsing) {
  grpc_chttp2_stream_global *stream_global;
  grpc_chttp2_stream_writing *stream_writing;

  GPR_TIMER_BEGIN("grpc_chttp2_unlocking_check_writes", 0);

  /* simple writes are queued to qbuf, and flushed here */
  gpr_slice_buffer_swap(&transport_global->qbuf, &transport_writing->outbuf);
  GPR_ASSERT(transport_global->qbuf.count == 0);

  grpc_chttp2_hpack_compressor_set_max_table_size(
      &transport_writing->hpack_compressor,
      transport_global->settings[GRPC_PEER_SETTINGS]
                                [GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE]);

  if (transport_global->dirtied_local_settings &&
      !transport_global->sent_local_settings && !is_parsing) {
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

  GRPC_CHTTP2_FLOW_MOVE_TRANSPORT("write", transport_writing, outgoing_window,
                                  transport_global, outgoing_window);
  bool is_window_available = transport_writing->outgoing_window > 0;
  grpc_chttp2_list_flush_writing_stalled_by_transport(
      exec_ctx, transport_writing, is_window_available);

  /* for each grpc_chttp2_stream that's become writable, frame it's data
     (according to available window sizes) and add to the output buffer */
  while (grpc_chttp2_list_pop_writable_stream(
      transport_global, transport_writing, &stream_global, &stream_writing)) {
    bool sent_initial_metadata = stream_writing->sent_initial_metadata;
    bool become_writable = false;

    stream_writing->id = stream_global->id;
    stream_writing->read_closed = stream_global->read_closed;

    GRPC_CHTTP2_FLOW_MOVE_STREAM("write", transport_writing, stream_writing,
                                 outgoing_window, stream_global,
                                 outgoing_window);

    if (!sent_initial_metadata && stream_global->send_initial_metadata) {
      stream_writing->send_initial_metadata =
          stream_global->send_initial_metadata;
      stream_global->send_initial_metadata = NULL;
      become_writable = true;
      sent_initial_metadata = true;
    }
    if (sent_initial_metadata) {
      if (stream_global->send_message != NULL) {
        gpr_slice hdr = gpr_slice_malloc(5);
        uint8_t *p = GPR_SLICE_START_PTR(hdr);
        uint32_t len = stream_global->send_message->length;
        GPR_ASSERT(stream_writing->send_message == NULL);
        p[0] = (stream_global->send_message->flags &
                GRPC_WRITE_INTERNAL_COMPRESS) != 0;
        p[1] = (uint8_t)(len >> 24);
        p[2] = (uint8_t)(len >> 16);
        p[3] = (uint8_t)(len >> 8);
        p[4] = (uint8_t)(len);
        gpr_slice_buffer_add(&stream_writing->flow_controlled_buffer, hdr);
        if (stream_global->send_message->length > 0) {
          stream_writing->send_message = stream_global->send_message;
        } else {
          stream_writing->send_message = NULL;
        }
        stream_writing->stream_fetched = 0;
        stream_global->send_message = NULL;
      }
      if ((stream_writing->send_message != NULL ||
           stream_writing->flow_controlled_buffer.length > 0) &&
          stream_writing->outgoing_window > 0) {
        if (transport_writing->outgoing_window > 0) {
          become_writable = true;
        } else {
          grpc_chttp2_list_add_stalled_by_transport(transport_writing,
                                                    stream_writing);
        }
      }
      if (stream_global->send_trailing_metadata) {
        stream_writing->send_trailing_metadata =
            stream_global->send_trailing_metadata;
        stream_global->send_trailing_metadata = NULL;
        become_writable = true;
      }
    }

    if (!stream_global->read_closed &&
        stream_global->unannounced_incoming_window_for_writing > 1024) {
      GRPC_CHTTP2_FLOW_MOVE_STREAM("write", transport_global, stream_writing,
                                   announce_window, stream_global,
                                   unannounced_incoming_window_for_writing);
      become_writable = true;
    }

    if (become_writable) {
      grpc_chttp2_list_add_writing_stream(transport_writing, stream_writing);
    } else {
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2_writing");
    }
  }

  /* if the grpc_chttp2_transport is ready to send a window update, do so here
     also; 3/4 is a magic number that will likely get tuned soon */
  if (transport_global->announce_incoming_window > 0) {
    uint32_t announced = (uint32_t)GPR_MIN(
        transport_global->announce_incoming_window, UINT32_MAX);
    GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT("write", transport_global,
                                     announce_incoming_window, announced);
    grpc_transport_one_way_stats throwaway_stats;
    gpr_slice_buffer_add(
        &transport_writing->outbuf,
        grpc_chttp2_window_update_create(0, announced, &throwaway_stats));
  }

  GPR_TIMER_END("grpc_chttp2_unlocking_check_writes", 0);

  return transport_writing->outbuf.count > 0 ||
         grpc_chttp2_list_have_writing_streams(transport_writing);
}

void grpc_chttp2_perform_writes(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_writing *transport_writing,
    grpc_endpoint *endpoint) {
  GPR_ASSERT(transport_writing->outbuf.count > 0 ||
             grpc_chttp2_list_have_writing_streams(transport_writing));

  finalize_outbuf(exec_ctx, transport_writing);

  GPR_ASSERT(endpoint);

  if (transport_writing->outbuf.count > 0) {
    grpc_endpoint_write(exec_ctx, endpoint, &transport_writing->outbuf,
                        &transport_writing->done_cb);
  } else {
    grpc_exec_ctx_enqueue(exec_ctx, &transport_writing->done_cb, true, NULL);
  }
}

static void finalize_outbuf(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_writing *transport_writing) {
  grpc_chttp2_stream_writing *stream_writing;

  GPR_TIMER_BEGIN("finalize_outbuf", 0);

  while (
      grpc_chttp2_list_pop_writing_stream(transport_writing, &stream_writing)) {
    uint32_t max_outgoing =
        (uint32_t)GPR_MIN(GRPC_CHTTP2_MAX_PAYLOAD_LENGTH,
                          GPR_MIN(stream_writing->outgoing_window,
                                  transport_writing->outgoing_window));
    /* send initial metadata if it's available */
    if (stream_writing->send_initial_metadata != NULL) {
      grpc_chttp2_encode_header(
          &transport_writing->hpack_compressor, stream_writing->id,
          stream_writing->send_initial_metadata, 0, &stream_writing->stats,
          &transport_writing->outbuf);
      stream_writing->send_initial_metadata = NULL;
      stream_writing->sent_initial_metadata = 1;
    }
    /* send any window updates */
    if (stream_writing->announce_window > 0 &&
        stream_writing->send_initial_metadata == NULL) {
      uint32_t announce = stream_writing->announce_window;
      gpr_slice_buffer_add(
          &transport_writing->outbuf,
          grpc_chttp2_window_update_create(stream_writing->id,
                                           stream_writing->announce_window,
                                           &stream_writing->stats));
      GRPC_CHTTP2_FLOW_DEBIT_STREAM("write", transport_writing, stream_writing,
                                    announce_window, announce);
      stream_writing->announce_window = 0;
    }
    /* fetch any body bytes */
    while (!stream_writing->fetching && stream_writing->send_message &&
           stream_writing->flow_controlled_buffer.length < max_outgoing &&
           stream_writing->stream_fetched <
               stream_writing->send_message->length) {
      if (grpc_byte_stream_next(exec_ctx, stream_writing->send_message,
                                &stream_writing->fetching_slice, max_outgoing,
                                &stream_writing->finished_fetch)) {
        stream_writing->stream_fetched +=
            GPR_SLICE_LENGTH(stream_writing->fetching_slice);
        if (stream_writing->stream_fetched ==
            stream_writing->send_message->length) {
          stream_writing->send_message = NULL;
        }
        gpr_slice_buffer_add(&stream_writing->flow_controlled_buffer,
                             stream_writing->fetching_slice);
      } else {
        stream_writing->fetching = 1;
      }
    }
    /* send any body bytes */
    if (stream_writing->flow_controlled_buffer.length > 0) {
      if (max_outgoing > 0) {
        uint32_t send_bytes = (uint32_t)GPR_MIN(
            max_outgoing, stream_writing->flow_controlled_buffer.length);
        int is_last_data_frame =
            stream_writing->send_message == NULL &&
            send_bytes == stream_writing->flow_controlled_buffer.length;
        int is_last_frame = is_last_data_frame &&
                            stream_writing->send_trailing_metadata != NULL &&
                            grpc_metadata_batch_is_empty(
                                stream_writing->send_trailing_metadata);
        grpc_chttp2_encode_data(
            stream_writing->id, &stream_writing->flow_controlled_buffer,
            send_bytes, is_last_frame, &stream_writing->stats,
            &transport_writing->outbuf);
        GRPC_CHTTP2_FLOW_DEBIT_STREAM("write", transport_writing,
                                      stream_writing, outgoing_window,
                                      send_bytes);
        GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT("write", transport_writing,
                                         outgoing_window, send_bytes);
        if (is_last_frame) {
          stream_writing->send_trailing_metadata = NULL;
          stream_writing->sent_trailing_metadata = 1;
        }
        if (is_last_data_frame) {
          GPR_ASSERT(stream_writing->send_message == NULL);
          stream_writing->sent_message = 1;
        }
      } else if (transport_writing->outgoing_window == 0) {
        grpc_chttp2_list_add_writing_stalled_by_transport(transport_writing,
                                                          stream_writing);
        grpc_chttp2_list_add_written_stream(transport_writing, stream_writing);
      }
    }
    /* send trailing metadata if it's available and we're ready for it */
    if (stream_writing->send_message == NULL &&
        stream_writing->flow_controlled_buffer.length == 0 &&
        stream_writing->send_trailing_metadata != NULL) {
      if (grpc_metadata_batch_is_empty(
              stream_writing->send_trailing_metadata)) {
        grpc_chttp2_encode_data(
            stream_writing->id, &stream_writing->flow_controlled_buffer, 0, 1,
            &stream_writing->stats, &transport_writing->outbuf);
      } else {
        grpc_chttp2_encode_header(
            &transport_writing->hpack_compressor, stream_writing->id,
            stream_writing->send_trailing_metadata, 1, &stream_writing->stats,
            &transport_writing->outbuf);
      }
      if (!transport_writing->is_client && !stream_writing->read_closed) {
        gpr_slice_buffer_add(&transport_writing->outbuf,
                             grpc_chttp2_rst_stream_create(
                                 stream_writing->id, GRPC_CHTTP2_NO_ERROR,
                                 &stream_writing->stats));
      }
      stream_writing->send_trailing_metadata = NULL;
      stream_writing->sent_trailing_metadata = 1;
    }
    /* if there's more to write, then loop, otherwise prepare to finish the
     * write */
    if ((stream_writing->flow_controlled_buffer.length > 0 ||
         (stream_writing->send_message && !stream_writing->fetching)) &&
        stream_writing->outgoing_window > 0) {
      if (transport_writing->outgoing_window > 0) {
        grpc_chttp2_list_add_writing_stream(transport_writing, stream_writing);
      } else {
        grpc_chttp2_list_add_writing_stalled_by_transport(transport_writing,
                                                          stream_writing);
        grpc_chttp2_list_add_written_stream(transport_writing, stream_writing);
      }
    } else {
      grpc_chttp2_list_add_written_stream(transport_writing, stream_writing);
    }
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
    if (stream_writing->sent_initial_metadata) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, stream_global,
          &stream_global->send_initial_metadata_finished, 1);
    }
    grpc_transport_move_one_way_stats(&stream_writing->stats,
                                      &stream_global->stats.outgoing);
    if (stream_writing->sent_message) {
      GPR_ASSERT(stream_writing->send_message == NULL);
      grpc_chttp2_complete_closure_step(
          exec_ctx, stream_global, &stream_global->send_message_finished, 1);
      stream_writing->sent_message = 0;
    }
    if (stream_writing->sent_trailing_metadata) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, stream_global,
          &stream_global->send_trailing_metadata_finished, 1);
    }
    if (stream_writing->sent_trailing_metadata) {
      grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global,
                                     !transport_global->is_client, 1);
    }
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2_writing");
  }
  gpr_slice_buffer_reset_and_unref(&transport_writing->outbuf);
}
