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

#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <limits.h>

#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/http2_errors.h"

static void add_to_write_list(grpc_chttp2_write_cb **list,
                              grpc_chttp2_write_cb *cb) {
  cb->next = *list;
  *list = cb;
}

static void finish_write_cb(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                            grpc_chttp2_stream *s, grpc_chttp2_write_cb *cb,
                            grpc_error *error) {
  grpc_chttp2_complete_closure_step(exec_ctx, t, s, &cb->closure, error,
                                    "finish_write_cb");
  cb->next = t->write_cb_pool;
  t->write_cb_pool = cb;
}

static void collapse_pings_from_into(grpc_chttp2_transport *t,
                                     grpc_chttp2_ping_type ping_type,
                                     grpc_chttp2_ping_queue *pq) {
  for (size_t i = 0; i < GRPC_CHTTP2_PCL_COUNT; i++) {
    grpc_closure_list_move(&t->ping_queues[ping_type].lists[i], &pq->lists[i]);
  }
}

static void maybe_initiate_ping(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_transport *t,
                                grpc_chttp2_ping_type ping_type) {
  grpc_chttp2_ping_queue *pq = &t->ping_queues[ping_type];
  if (grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_NEXT])) {
    /* no ping needed: wait */
    return;
  }
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_INFLIGHT])) {
    /* ping already in-flight: wait */
    if (GRPC_TRACER_ON(grpc_http_trace) ||
        GRPC_TRACER_ON(grpc_bdp_estimator_trace)) {
      gpr_log(GPR_DEBUG, "Ping delayed [%p]: already pinging", t->peer_string);
    }
    return;
  }
  if (t->ping_state.pings_before_data_required == 0 &&
      t->ping_policy.max_pings_without_data != 0) {
    /* need to send something of substance before sending a ping again */
    if (GRPC_TRACER_ON(grpc_http_trace) ||
        GRPC_TRACER_ON(grpc_bdp_estimator_trace)) {
      gpr_log(GPR_DEBUG, "Ping delayed [%p]: too many recent pings: %d/%d",
              t->peer_string, t->ping_state.pings_before_data_required,
              t->ping_policy.max_pings_without_data);
    }
    return;
  }
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_timespec elapsed = gpr_time_sub(now, t->ping_state.last_ping_sent_time);
  /*gpr_log(GPR_DEBUG, "elapsed:%d.%09d min:%d.%09d", (int)elapsed.tv_sec,
          elapsed.tv_nsec, (int)t->ping_policy.min_time_between_pings.tv_sec,
          (int)t->ping_policy.min_time_between_pings.tv_nsec);*/
  if (gpr_time_cmp(elapsed, t->ping_policy.min_time_between_pings) < 0) {
    /* not enough elapsed time between successive pings */
    if (GRPC_TRACER_ON(grpc_http_trace) ||
        GRPC_TRACER_ON(grpc_bdp_estimator_trace)) {
      gpr_log(GPR_DEBUG,
              "Ping delayed [%p]: not enough time elapsed since last ping",
              t->peer_string);
    }
    if (!t->ping_state.is_delayed_ping_timer_set) {
      t->ping_state.is_delayed_ping_timer_set = true;
      grpc_timer_init(exec_ctx, &t->ping_state.delayed_ping_timer,
                      gpr_time_add(t->ping_state.last_ping_sent_time,
                                   t->ping_policy.min_time_between_pings),
                      &t->retry_initiate_ping_locked,
                      gpr_now(GPR_CLOCK_MONOTONIC));
    }
    return;
  }
  /* coalesce equivalent pings into this one */
  switch (ping_type) {
    case GRPC_CHTTP2_PING_BEFORE_TRANSPORT_WINDOW_UPDATE:
      collapse_pings_from_into(t, GRPC_CHTTP2_PING_ON_NEXT_WRITE, pq);
      break;
    case GRPC_CHTTP2_PING_ON_NEXT_WRITE:
      break;
    case GRPC_CHTTP2_PING_TYPE_COUNT:
      GPR_UNREACHABLE_CODE(break);
  }
  pq->inflight_id = t->ping_ctr * GRPC_CHTTP2_PING_TYPE_COUNT + ping_type;
  t->ping_ctr++;
  GRPC_CLOSURE_LIST_SCHED(exec_ctx, &pq->lists[GRPC_CHTTP2_PCL_INITIATE]);
  grpc_closure_list_move(&pq->lists[GRPC_CHTTP2_PCL_NEXT],
                         &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT]);
  grpc_slice_buffer_add(&t->outbuf,
                        grpc_chttp2_ping_create(false, pq->inflight_id));
  t->ping_state.last_ping_sent_time = now;
  t->ping_state.pings_before_data_required -=
      (t->ping_state.pings_before_data_required != 0);
}

static void update_list(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                        grpc_chttp2_stream *s, int64_t send_bytes,
                        grpc_chttp2_write_cb **list, grpc_error *error) {
  grpc_chttp2_write_cb *cb = *list;
  *list = NULL;
  s->flow_controlled_bytes_written += send_bytes;
  while (cb) {
    grpc_chttp2_write_cb *next = cb->next;
    if (cb->call_at_byte <= s->flow_controlled_bytes_written) {
      finish_write_cb(exec_ctx, t, s, cb, GRPC_ERROR_REF(error));
    } else {
      add_to_write_list(list, cb);
    }
    cb = next;
  }
  GRPC_ERROR_UNREF(error);
}

static bool stream_ref_if_not_destroyed(gpr_refcount *r) {
  gpr_atm count;
  do {
    count = gpr_atm_acq_load(&r->count);
    if (count == 0) return false;
  } while (!gpr_atm_rel_cas(&r->count, count, count + 1));
  return true;
}

/* How many bytes would we like to put on the wire during a single syscall */
static uint32_t target_write_size(grpc_chttp2_transport *t) {
  return 1024 * 1024;
}

// Returns true if initial_metadata contains only default headers.
//
// TODO(roth): The fact that we hard-code these particular headers here
// is fairly ugly.  Need some better way to know which headers are
// default, maybe via a bit in the static metadata table?
static bool is_default_initial_metadata(grpc_metadata_batch *initial_metadata) {
  int num_default_fields =
      (initial_metadata->idx.named.status != NULL) +
      (initial_metadata->idx.named.content_type != NULL) +
      (initial_metadata->idx.named.grpc_encoding != NULL) +
      (initial_metadata->idx.named.grpc_accept_encoding != NULL);
  return (size_t)num_default_fields == initial_metadata->list.count;
}

grpc_chttp2_begin_write_result grpc_chttp2_begin_write(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t) {
  grpc_chttp2_stream *s;

  GPR_TIMER_BEGIN("grpc_chttp2_begin_write", 0);

  if (t->dirtied_local_settings && !t->sent_local_settings) {
    grpc_slice_buffer_add(
        &t->outbuf,
        grpc_chttp2_settings_create(
            t->settings[GRPC_SENT_SETTINGS], t->settings[GRPC_LOCAL_SETTINGS],
            t->force_send_settings, GRPC_CHTTP2_NUM_SETTINGS));
    t->force_send_settings = 0;
    t->dirtied_local_settings = 0;
    t->sent_local_settings = 1;
  }

  /* simple writes are queued to qbuf, and flushed here */
  grpc_slice_buffer_move_into(&t->qbuf, &t->outbuf);
  GPR_ASSERT(t->qbuf.count == 0);

  grpc_chttp2_hpack_compressor_set_max_table_size(
      &t->hpack_compressor,
      t->settings[GRPC_PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE]);

  if (t->flow_control.remote_window > 0) {
    while (grpc_chttp2_list_pop_stalled_by_transport(t, &s)) {
      if (!t->closed && grpc_chttp2_list_add_writable_stream(t, s) &&
          stream_ref_if_not_destroyed(&s->refcount->refs)) {
        grpc_chttp2_initiate_write(exec_ctx, t, "transport.read_flow_control");
      }
    }
  }

  bool partial_write = false;

  /* for each grpc_chttp2_stream that's become writable, frame it's data
     (according to available window sizes) and add to the output buffer */
  while (true) {
    if (t->outbuf.length > target_write_size(t)) {
      partial_write = true;
      break;
    }

    if (!grpc_chttp2_list_pop_writable_stream(t, &s)) {
      break;
    }

    bool sent_initial_metadata = s->sent_initial_metadata;
    bool now_writing = false;

    GRPC_CHTTP2_IF_TRACING(
        gpr_log(GPR_DEBUG, "W:%p %s[%d] im-(sent,send)=(%d,%d) announce=%d", t,
                t->is_client ? "CLIENT" : "SERVER", s->id,
                sent_initial_metadata, s->send_initial_metadata != NULL,
                (int)(s->flow_control.local_window_delta -
                      s->flow_control.announced_window_delta)));

    grpc_mdelem *extra_headers_for_trailing_metadata[2];
    size_t num_extra_headers_for_trailing_metadata = 0;

    /* send initial metadata if it's available */
    if (!sent_initial_metadata && s->send_initial_metadata != NULL) {
      // We skip this on the server side if there is no custom initial
      // metadata, there are no messages to send, and we are also sending
      // trailing metadata.  This results in a Trailers-Only response,
      // which is required for retries, as per:
      // https://github.com/grpc/proposal/blob/master/A6-client-retries.md#when-retries-are-valid
      if (t->is_client || s->fetching_send_message != NULL ||
          s->flow_controlled_buffer.length != 0 ||
          s->send_trailing_metadata == NULL ||
          !is_default_initial_metadata(s->send_initial_metadata)) {
        grpc_encode_header_options hopt = {
            .stream_id = s->id,
            .is_eof = false,
            .use_true_binary_metadata =
                t->settings
                    [GRPC_PEER_SETTINGS]
                    [GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA] != 0,
            .max_frame_size = t->settings[GRPC_PEER_SETTINGS]
                                         [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
            .stats = &s->stats.outgoing};
        grpc_chttp2_encode_header(exec_ctx, &t->hpack_compressor, NULL, 0,
                                  s->send_initial_metadata, &hopt, &t->outbuf);
        now_writing = true;
        t->ping_state.pings_before_data_required =
            t->ping_policy.max_pings_without_data;
        if (!t->is_client) {
          t->ping_recv_state.last_ping_recv_time =
              gpr_inf_past(GPR_CLOCK_MONOTONIC);
          t->ping_recv_state.ping_strikes = 0;
        }
      } else {
        GRPC_CHTTP2_IF_TRACING(
            gpr_log(GPR_INFO, "not sending initial_metadata (Trailers-Only)"));
        // When sending Trailers-Only, we need to move the :status and
        // content-type headers to the trailers.
        if (s->send_initial_metadata->idx.named.status != NULL) {
          extra_headers_for_trailing_metadata
              [num_extra_headers_for_trailing_metadata++] =
                  &s->send_initial_metadata->idx.named.status->md;
        }
        if (s->send_initial_metadata->idx.named.content_type != NULL) {
          extra_headers_for_trailing_metadata
              [num_extra_headers_for_trailing_metadata++] =
                  &s->send_initial_metadata->idx.named.content_type->md;
        }
      }
      s->send_initial_metadata = NULL;
      s->sent_initial_metadata = true;
      sent_initial_metadata = true;
    }
    /* send any window updates */
    uint32_t stream_announce = grpc_chttp2_flowctl_maybe_send_stream_update(
        &t->flow_control, &s->flow_control);
    if (stream_announce > 0) {
      grpc_slice_buffer_add(
          &t->outbuf, grpc_chttp2_window_update_create(s->id, stream_announce,
                                                       &s->stats.outgoing));
      t->ping_state.pings_before_data_required =
          t->ping_policy.max_pings_without_data;
      if (!t->is_client) {
        t->ping_recv_state.last_ping_recv_time =
            gpr_inf_past(GPR_CLOCK_MONOTONIC);
        t->ping_recv_state.ping_strikes = 0;
      }
    }
    if (sent_initial_metadata) {
      /* send any body bytes, if allowed by flow control */
      if (s->flow_controlled_buffer.length > 0 ||
          (s->stream_compression_send_enabled &&
           s->compressed_data_buffer->length > 0)) {
        uint32_t stream_remote_window = (uint32_t)GPR_MAX(
            0,
            s->flow_control.remote_window_delta +
                (int64_t)t->settings[GRPC_PEER_SETTINGS]
                                    [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]);
        uint32_t max_outgoing = (uint32_t)GPR_MIN(
            t->settings[GRPC_PEER_SETTINGS]
                       [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
            GPR_MIN(stream_remote_window, t->flow_control.remote_window));
        if (max_outgoing > 0) {
          bool is_last_data_frame = false;
          bool is_last_frame = false;
          if (s->stream_compression_send_enabled) {
            while ((s->flow_controlled_buffer.length > 0 ||
                    s->compressed_data_buffer->length > 0) &&
                   max_outgoing > 0) {
              if (s->compressed_data_buffer->length > 0) {
                uint32_t send_bytes = (uint32_t)GPR_MIN(
                    max_outgoing, s->compressed_data_buffer->length);
                is_last_data_frame =
                    (send_bytes == s->compressed_data_buffer->length &&
                     s->flow_controlled_buffer.length == 0 &&
                     s->fetching_send_message == NULL);
                is_last_frame =
                    is_last_data_frame && s->send_trailing_metadata != NULL &&
                    grpc_metadata_batch_is_empty(s->send_trailing_metadata);
                grpc_chttp2_encode_data(s->id, s->compressed_data_buffer,
                                        send_bytes, is_last_frame,
                                        &s->stats.outgoing, &t->outbuf);
                grpc_chttp2_flowctl_sent_data(&t->flow_control,
                                              &s->flow_control, send_bytes);
                max_outgoing -= send_bytes;
                if (s->compressed_data_buffer->length == 0) {
                  s->sending_bytes += s->uncompressed_data_size;
                }
              } else {
                if (s->stream_compression_ctx == NULL) {
                  s->stream_compression_ctx =
                      grpc_stream_compression_context_create(
                          GRPC_STREAM_COMPRESSION_COMPRESS);
                }
                s->uncompressed_data_size = s->flow_controlled_buffer.length;
                GPR_ASSERT(grpc_stream_compress(
                    s->stream_compression_ctx, &s->flow_controlled_buffer,
                    s->compressed_data_buffer, NULL, MAX_SIZE_T,
                    GRPC_STREAM_COMPRESSION_FLUSH_SYNC));
              }
            }
          } else {
            uint32_t send_bytes = (uint32_t)GPR_MIN(
                max_outgoing, s->flow_controlled_buffer.length);
            is_last_data_frame = s->fetching_send_message == NULL &&
                                 send_bytes == s->flow_controlled_buffer.length;
            is_last_frame =
                is_last_data_frame && s->send_trailing_metadata != NULL &&
                grpc_metadata_batch_is_empty(s->send_trailing_metadata);
            grpc_chttp2_encode_data(s->id, &s->flow_controlled_buffer,
                                    send_bytes, is_last_frame,
                                    &s->stats.outgoing, &t->outbuf);
            grpc_chttp2_flowctl_sent_data(&t->flow_control, &s->flow_control,
                                          send_bytes);
            s->sending_bytes += send_bytes;
          }
          t->ping_state.pings_before_data_required =
              t->ping_policy.max_pings_without_data;
          if (!t->is_client) {
            t->ping_recv_state.last_ping_recv_time =
                gpr_inf_past(GPR_CLOCK_MONOTONIC);
            t->ping_recv_state.ping_strikes = 0;
          }
          if (is_last_frame) {
            s->send_trailing_metadata = NULL;
            s->sent_trailing_metadata = true;
            if (!t->is_client && !s->read_closed) {
              grpc_slice_buffer_add(&t->outbuf, grpc_chttp2_rst_stream_create(
                                                    s->id, GRPC_HTTP2_NO_ERROR,
                                                    &s->stats.outgoing));
            }
          }
          now_writing = true;
          if (s->flow_controlled_buffer.length > 0 ||
              (s->stream_compression_send_enabled &&
               s->compressed_data_buffer->length > 0)) {
            GRPC_CHTTP2_STREAM_REF(s, "chttp2_writing:fork");
            grpc_chttp2_list_add_writable_stream(t, s);
          }
        } else if (t->flow_control.remote_window == 0) {
          grpc_chttp2_list_add_stalled_by_transport(t, s);
          now_writing = true;
        } else if (stream_remote_window == 0) {
          grpc_chttp2_list_add_stalled_by_stream(t, s);
          now_writing = true;
        }
      }
      if (s->send_trailing_metadata != NULL &&
          s->fetching_send_message == NULL &&
          s->flow_controlled_buffer.length == 0 &&
          (!s->stream_compression_send_enabled ||
           s->compressed_data_buffer->length == 0)) {
        GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "sending trailing_metadata"));
        if (grpc_metadata_batch_is_empty(s->send_trailing_metadata)) {
          grpc_chttp2_encode_data(s->id, &s->flow_controlled_buffer, 0, true,
                                  &s->stats.outgoing, &t->outbuf);
        } else {
          grpc_encode_header_options hopt = {
              .stream_id = s->id,
              .is_eof = true,
              .use_true_binary_metadata =
                  t->settings
                      [GRPC_PEER_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA] !=
                  0,
              .max_frame_size =
                  t->settings[GRPC_PEER_SETTINGS]
                             [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
              .stats = &s->stats.outgoing};
          grpc_chttp2_encode_header(exec_ctx, &t->hpack_compressor,
                                    extra_headers_for_trailing_metadata,
                                    num_extra_headers_for_trailing_metadata,
                                    s->send_trailing_metadata, &hopt,
                                    &t->outbuf);
        }
        s->send_trailing_metadata = NULL;
        s->sent_trailing_metadata = true;
        if (!t->is_client && !s->read_closed) {
          grpc_slice_buffer_add(
              &t->outbuf, grpc_chttp2_rst_stream_create(
                              s->id, GRPC_HTTP2_NO_ERROR, &s->stats.outgoing));
        }
        now_writing = true;
      }
    }

    if (now_writing) {
      if (!grpc_chttp2_list_add_writing_stream(t, s)) {
        /* already in writing list: drop ref */
        GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:already_writing");
      }
    } else {
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:no_write");
    }
  }

  uint32_t transport_announce =
      grpc_chttp2_flowctl_maybe_send_transport_update(&t->flow_control);
  if (transport_announce) {
    maybe_initiate_ping(exec_ctx, t,
                        GRPC_CHTTP2_PING_BEFORE_TRANSPORT_WINDOW_UPDATE);
    grpc_transport_one_way_stats throwaway_stats;
    grpc_slice_buffer_add(
        &t->outbuf, grpc_chttp2_window_update_create(0, transport_announce,
                                                     &throwaway_stats));
    t->ping_state.pings_before_data_required =
        t->ping_policy.max_pings_without_data;
    if (!t->is_client) {
      t->ping_recv_state.last_ping_recv_time =
          gpr_inf_past(GPR_CLOCK_MONOTONIC);
      t->ping_recv_state.ping_strikes = 0;
    }
  }

  for (size_t i = 0; i < t->ping_ack_count; i++) {
    grpc_slice_buffer_add(&t->outbuf,
                          grpc_chttp2_ping_create(1, t->ping_acks[i]));
  }
  t->ping_ack_count = 0;

  maybe_initiate_ping(exec_ctx, t, GRPC_CHTTP2_PING_ON_NEXT_WRITE);

  GPR_TIMER_END("grpc_chttp2_begin_write", 0);

  return t->outbuf.count > 0 ? (partial_write ? GRPC_CHTTP2_PARTIAL_WRITE
                                              : GRPC_CHTTP2_FULL_WRITE)
                             : GRPC_CHTTP2_NOTHING_TO_WRITE;
}

void grpc_chttp2_end_write(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                           grpc_error *error) {
  GPR_TIMER_BEGIN("grpc_chttp2_end_write", 0);
  grpc_chttp2_stream *s;

  while (grpc_chttp2_list_pop_writing_stream(t, &s)) {
    if (s->sent_initial_metadata) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, t, s, &s->send_initial_metadata_finished,
          GRPC_ERROR_REF(error), "send_initial_metadata_finished");
    }
    if (s->sending_bytes != 0) {
      update_list(exec_ctx, t, s, (int64_t)s->sending_bytes,
                  &s->on_write_finished_cbs, GRPC_ERROR_REF(error));
      s->sending_bytes = 0;
    }
    if (s->sent_trailing_metadata) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, t, s, &s->send_trailing_metadata_finished,
          GRPC_ERROR_REF(error), "send_trailing_metadata_finished");
      grpc_chttp2_mark_stream_closed(exec_ctx, t, s, !t->is_client, 1,
                                     GRPC_ERROR_REF(error));
    }
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:end");
  }
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &t->outbuf);
  GRPC_ERROR_UNREF(error);
  GPR_TIMER_END("grpc_chttp2_end_write", 0);
}
