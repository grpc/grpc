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

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/http2_errors.h"

static void add_to_write_list(grpc_chttp2_write_cb** list,
                              grpc_chttp2_write_cb* cb) {
  cb->next = *list;
  *list = cb;
}

static void finish_write_cb(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                            grpc_chttp2_write_cb* cb, grpc_error* error) {
  grpc_chttp2_complete_closure_step(t, s, &cb->closure, error,
                                    "finish_write_cb");
  cb->next = t->write_cb_pool;
  t->write_cb_pool = cb;
}

static void maybe_initiate_ping(grpc_chttp2_transport* t) {
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_NEXT])) {
    /* no ping needed: wait */
    return;
  }
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_INFLIGHT])) {
    /* ping already in-flight: wait */
    if (grpc_http_trace.enabled() || grpc_bdp_estimator_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%s: Ping delayed [%p]: already pinging",
              t->is_client ? "CLIENT" : "SERVER", t->peer_string);
    }
    return;
  }
  if (t->ping_state.pings_before_data_required == 0 &&
      t->ping_policy.max_pings_without_data != 0) {
    /* need to receive something of substance before sending a ping again */
    if (grpc_http_trace.enabled() || grpc_bdp_estimator_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%s: Ping delayed [%p]: too many recent pings: %d/%d",
              t->is_client ? "CLIENT" : "SERVER", t->peer_string,
              t->ping_state.pings_before_data_required,
              t->ping_policy.max_pings_without_data);
    }
    return;
  }
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();

  grpc_millis next_allowed_ping_interval =
      (t->keepalive_permit_without_calls == 0 &&
       grpc_chttp2_stream_map_size(&t->stream_map) == 0)
          ? 7200 * GPR_MS_PER_SEC
          : t->ping_policy.min_sent_ping_interval_without_data;
  grpc_millis next_allowed_ping =
      t->ping_state.last_ping_sent_time + next_allowed_ping_interval;

  if (next_allowed_ping > now) {
    /* not enough elapsed time between successive pings */
    if (grpc_http_trace.enabled() || grpc_bdp_estimator_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "%s: Ping delayed [%p]: not enough time elapsed since last ping. "
              " Last ping %f: Next ping %f: Now %f",
              t->is_client ? "CLIENT" : "SERVER", t->peer_string,
              static_cast<double>(t->ping_state.last_ping_sent_time),
              static_cast<double>(next_allowed_ping), static_cast<double>(now));
    }
    if (!t->ping_state.is_delayed_ping_timer_set) {
      t->ping_state.is_delayed_ping_timer_set = true;
      GRPC_CHTTP2_REF_TRANSPORT(t, "retry_initiate_ping_locked");
      grpc_timer_init(&t->ping_state.delayed_ping_timer, next_allowed_ping,
                      &t->retry_initiate_ping_locked);
    }
    return;
  }

  pq->inflight_id = t->ping_ctr;
  t->ping_ctr++;
  GRPC_CLOSURE_LIST_SCHED(&pq->lists[GRPC_CHTTP2_PCL_INITIATE]);
  grpc_closure_list_move(&pq->lists[GRPC_CHTTP2_PCL_NEXT],
                         &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT]);
  grpc_slice_buffer_add(&t->outbuf,
                        grpc_chttp2_ping_create(false, pq->inflight_id));
  GRPC_STATS_INC_HTTP2_PINGS_SENT();
  t->ping_state.last_ping_sent_time = now;
  if (grpc_http_trace.enabled() || grpc_bdp_estimator_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s: Ping sent [%p]: %d/%d",
            t->is_client ? "CLIENT" : "SERVER", t->peer_string,
            t->ping_state.pings_before_data_required,
            t->ping_policy.max_pings_without_data);
  }
  t->ping_state.pings_before_data_required -=
      (t->ping_state.pings_before_data_required != 0);
}

static bool update_list(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                        int64_t send_bytes, grpc_chttp2_write_cb** list,
                        int64_t* ctr, grpc_error* error) {
  bool sched_any = false;
  grpc_chttp2_write_cb* cb = *list;
  *list = nullptr;
  *ctr += send_bytes;
  while (cb) {
    grpc_chttp2_write_cb* next = cb->next;
    if (cb->call_at_byte <= *ctr) {
      sched_any = true;
      finish_write_cb(t, s, cb, GRPC_ERROR_REF(error));
    } else {
      add_to_write_list(list, cb);
    }
    cb = next;
  }
  GRPC_ERROR_UNREF(error);
  return sched_any;
}

static void report_stall(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                         const char* staller) {
  gpr_log(
      GPR_DEBUG,
      "%s:%p stream %d stalled by %s [fc:pending=%" PRIdPTR
      ":pending-compressed=%" PRIdPTR ":flowed=%" PRId64
      ":peer_initwin=%d:t_win=%" PRId64 ":s_win=%d:s_delta=%" PRId64 "]",
      t->peer_string, t, s->id, staller, s->flow_controlled_buffer.length,
      s->compressed_data_buffer.length, s->flow_controlled_bytes_flowed,
      t->settings[GRPC_ACKED_SETTINGS]
                 [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
      t->flow_control->remote_window(),
      static_cast<uint32_t> GPR_MAX(
          0,
          s->flow_control->remote_window_delta() +
              (int64_t)t->settings[GRPC_PEER_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]),
      s->flow_control->remote_window_delta());
}

static bool stream_ref_if_not_destroyed(gpr_refcount* r) {
  gpr_atm count;
  do {
    count = gpr_atm_acq_load(&r->count);
    if (count == 0) return false;
  } while (!gpr_atm_rel_cas(&r->count, count, count + 1));
  return true;
}

/* How many bytes would we like to put on the wire during a single syscall */
static uint32_t target_write_size(grpc_chttp2_transport* t) {
  return 1024 * 1024;
}

// Returns true if initial_metadata contains only default headers.
static bool is_default_initial_metadata(grpc_metadata_batch* initial_metadata) {
  return initial_metadata->list.default_count == initial_metadata->list.count;
}

namespace {
class StreamWriteContext;

class WriteContext {
 public:
  WriteContext(grpc_chttp2_transport* t) : t_(t) {
    GRPC_STATS_INC_HTTP2_WRITES_BEGUN();
    GPR_TIMER_SCOPE("grpc_chttp2_begin_write", 0);
  }

  // TODO(ctiller): make this the destructor
  void FlushStats() {
    GRPC_STATS_INC_HTTP2_SEND_INITIAL_METADATA_PER_WRITE(
        initial_metadata_writes_);
    GRPC_STATS_INC_HTTP2_SEND_MESSAGE_PER_WRITE(message_writes_);
    GRPC_STATS_INC_HTTP2_SEND_TRAILING_METADATA_PER_WRITE(
        trailing_metadata_writes_);
    GRPC_STATS_INC_HTTP2_SEND_FLOWCTL_PER_WRITE(flow_control_writes_);
  }

  void FlushSettings() {
    if (t_->dirtied_local_settings && !t_->sent_local_settings) {
      grpc_slice_buffer_add(
          &t_->outbuf, grpc_chttp2_settings_create(
                           t_->settings[GRPC_SENT_SETTINGS],
                           t_->settings[GRPC_LOCAL_SETTINGS],
                           t_->force_send_settings, GRPC_CHTTP2_NUM_SETTINGS));
      t_->force_send_settings = false;
      t_->dirtied_local_settings = false;
      t_->sent_local_settings = true;
      GRPC_STATS_INC_HTTP2_SETTINGS_WRITES();
    }
  }

  void FlushQueuedBuffers() {
    /* simple writes are queued to qbuf, and flushed here */
    grpc_slice_buffer_move_into(&t_->qbuf, &t_->outbuf);
    GPR_ASSERT(t_->qbuf.count == 0);
  }

  void FlushWindowUpdates() {
    uint32_t transport_announce =
        t_->flow_control->MaybeSendUpdate(t_->outbuf.count > 0);
    if (transport_announce) {
      grpc_transport_one_way_stats throwaway_stats;
      grpc_slice_buffer_add(
          &t_->outbuf, grpc_chttp2_window_update_create(0, transport_announce,
                                                        &throwaway_stats));
      ResetPingRecvClock();
    }
  }

  void FlushPingAcks() {
    for (size_t i = 0; i < t_->ping_ack_count; i++) {
      grpc_slice_buffer_add(&t_->outbuf,
                            grpc_chttp2_ping_create(true, t_->ping_acks[i]));
    }
    t_->ping_ack_count = 0;
  }

  void EnactHpackSettings() {
    grpc_chttp2_hpack_compressor_set_max_table_size(
        &t_->hpack_compressor,
        t_->settings[GRPC_PEER_SETTINGS]
                    [GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE]);
  }

  void UpdateStreamsNoLongerStalled() {
    grpc_chttp2_stream* s;
    while (grpc_chttp2_list_pop_stalled_by_transport(t_, &s)) {
      if (t_->closed_with_error == GRPC_ERROR_NONE &&
          grpc_chttp2_list_add_writable_stream(t_, s)) {
        if (!stream_ref_if_not_destroyed(&s->refcount->refs)) {
          grpc_chttp2_list_remove_writable_stream(t_, s);
        }
      }
    }
  }

  grpc_chttp2_stream* NextStream() {
    if (t_->outbuf.length > target_write_size(t_)) {
      result_.partial = true;
      return nullptr;
    }

    grpc_chttp2_stream* s;
    if (!grpc_chttp2_list_pop_writable_stream(t_, &s)) {
      return nullptr;
    }

    return s;
  }

  void ResetPingRecvClock() {
    if (!t_->is_client) {
      t_->ping_recv_state.last_ping_recv_time = GRPC_MILLIS_INF_PAST;
      t_->ping_recv_state.ping_strikes = 0;
    }
  }

  void IncInitialMetadataWrites() { ++initial_metadata_writes_; }
  void IncWindowUpdateWrites() { ++flow_control_writes_; }
  void IncMessageWrites() { ++message_writes_; }
  void IncTrailingMetadataWrites() { ++trailing_metadata_writes_; }

  void NoteScheduledResults() { result_.early_results_scheduled = true; }

  grpc_chttp2_transport* transport() const { return t_; }

  grpc_chttp2_begin_write_result Result() {
    result_.writing = t_->outbuf.count > 0;
    return result_;
  }

 private:
  grpc_chttp2_transport* const t_;

  /* stats histogram counters: we increment these throughout this function,
     and at the end publish to the central stats histograms */
  int flow_control_writes_ = 0;
  int initial_metadata_writes_ = 0;
  int trailing_metadata_writes_ = 0;
  int message_writes_ = 0;
  grpc_chttp2_begin_write_result result_ = {false, false, false};
};

class DataSendContext {
 public:
  DataSendContext(WriteContext* write_context, grpc_chttp2_transport* t,
                  grpc_chttp2_stream* s)
      : write_context_(write_context),
        t_(t),
        s_(s),
        sending_bytes_before_(s_->sending_bytes) {}

  uint32_t stream_remote_window() const {
    return static_cast<uint32_t> GPR_MAX(
        0, s_->flow_control->remote_window_delta() +
               (int64_t)t_->settings[GRPC_PEER_SETTINGS]
                                    [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE]);
  }

  uint32_t max_outgoing() const {
    return static_cast<uint32_t> GPR_MIN(
        t_->settings[GRPC_PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
        GPR_MIN(stream_remote_window(), t_->flow_control->remote_window()));
  }

  bool AnyOutgoing() const { return max_outgoing() > 0; }

  void FlushCompressedBytes() {
    uint32_t send_bytes = static_cast<uint32_t> GPR_MIN(
        max_outgoing(), s_->compressed_data_buffer.length);
    bool is_last_data_frame =
        (send_bytes == s_->compressed_data_buffer.length &&
         s_->flow_controlled_buffer.length == 0 &&
         s_->fetching_send_message == nullptr);
    if (is_last_data_frame && s_->send_trailing_metadata != nullptr &&
        s_->stream_compression_ctx != nullptr) {
      if (!grpc_stream_compress(
              s_->stream_compression_ctx, &s_->flow_controlled_buffer,
              &s_->compressed_data_buffer, nullptr, MAX_SIZE_T,
              GRPC_STREAM_COMPRESSION_FLUSH_FINISH)) {
        gpr_log(GPR_ERROR, "Stream compression failed.");
      }
      grpc_stream_compression_context_destroy(s_->stream_compression_ctx);
      s_->stream_compression_ctx = nullptr;
      /* After finish, bytes in s->compressed_data_buffer may be
       * more than max_outgoing. Start another round of the current
       * while loop so that send_bytes and is_last_data_frame are
       * recalculated. */
      return;
    }
    is_last_frame_ = is_last_data_frame &&
                     s_->send_trailing_metadata != nullptr &&
                     grpc_metadata_batch_is_empty(s_->send_trailing_metadata);
    grpc_chttp2_encode_data(s_->id, &s_->compressed_data_buffer, send_bytes,
                            is_last_frame_, &s_->stats.outgoing, &t_->outbuf);
    s_->flow_control->SentData(send_bytes);
    if (s_->compressed_data_buffer.length == 0) {
      s_->sending_bytes += s_->uncompressed_data_size;
    }
  }

  void CompressMoreBytes() {
    if (s_->stream_compression_ctx == nullptr) {
      s_->stream_compression_ctx =
          grpc_stream_compression_context_create(s_->stream_compression_method);
    }
    s_->uncompressed_data_size = s_->flow_controlled_buffer.length;
    if (!grpc_stream_compress(s_->stream_compression_ctx,
                              &s_->flow_controlled_buffer,
                              &s_->compressed_data_buffer, nullptr, MAX_SIZE_T,
                              GRPC_STREAM_COMPRESSION_FLUSH_SYNC)) {
      gpr_log(GPR_ERROR, "Stream compression failed.");
    }
  }

  bool is_last_frame() const { return is_last_frame_; }

  void CallCallbacks() {
    if (update_list(
            t_, s_,
            static_cast<int64_t>(s_->sending_bytes - sending_bytes_before_),
            &s_->on_flow_controlled_cbs, &s_->flow_controlled_bytes_flowed,
            GRPC_ERROR_NONE)) {
      write_context_->NoteScheduledResults();
    }
  }

 private:
  WriteContext* write_context_;
  grpc_chttp2_transport* t_;
  grpc_chttp2_stream* s_;
  const size_t sending_bytes_before_;
  bool is_last_frame_ = false;
};

class StreamWriteContext {
 public:
  StreamWriteContext(WriteContext* write_context, grpc_chttp2_stream* s)
      : write_context_(write_context), t_(write_context->transport()), s_(s) {
    GRPC_CHTTP2_IF_TRACING(
        gpr_log(GPR_DEBUG, "W:%p %s[%d] im-(sent,send)=(%d,%d) announce=%d", t_,
                t_->is_client ? "CLIENT" : "SERVER", s->id,
                s->sent_initial_metadata, s->send_initial_metadata != nullptr,
                (int)(s->flow_control->local_window_delta() -
                      s->flow_control->announced_window_delta())));
  }

  void FlushInitialMetadata() {
    /* send initial metadata if it's available */
    if (s_->sent_initial_metadata) return;
    if (s_->send_initial_metadata == nullptr) return;

    // We skip this on the server side if there is no custom initial
    // metadata, there are no messages to send, and we are also sending
    // trailing metadata.  This results in a Trailers-Only response,
    // which is required for retries, as per:
    // https://github.com/grpc/proposal/blob/master/A6-client-retries.md#when-retries-are-valid
    if (!t_->is_client && s_->fetching_send_message == nullptr &&
        s_->flow_controlled_buffer.length == 0 &&
        s_->compressed_data_buffer.length == 0 &&
        s_->send_trailing_metadata != nullptr &&
        is_default_initial_metadata(s_->send_initial_metadata)) {
      ConvertInitialMetadataToTrailingMetadata();
    } else {
      grpc_encode_header_options hopt = {
          s_->id,  // stream_id
          false,   // is_eof
          t_->settings[GRPC_PEER_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA] !=
              0,  // use_true_binary_metadata
          t_->settings[GRPC_PEER_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],  // max_frame_size
          &s_->stats.outgoing                                 // stats
      };
      grpc_chttp2_encode_header(&t_->hpack_compressor, nullptr, 0,
                                s_->send_initial_metadata, &hopt, &t_->outbuf);
      write_context_->ResetPingRecvClock();
      write_context_->IncInitialMetadataWrites();
    }

    s_->send_initial_metadata = nullptr;
    s_->sent_initial_metadata = true;
    write_context_->NoteScheduledResults();
    grpc_chttp2_complete_closure_step(
        t_, s_, &s_->send_initial_metadata_finished, GRPC_ERROR_NONE,
        "send_initial_metadata_finished");
  }

  void FlushWindowUpdates() {
    /* send any window updates */
    const uint32_t stream_announce = s_->flow_control->MaybeSendUpdate();
    if (stream_announce == 0) return;

    grpc_slice_buffer_add(
        &t_->outbuf, grpc_chttp2_window_update_create(s_->id, stream_announce,
                                                      &s_->stats.outgoing));
    write_context_->ResetPingRecvClock();
    write_context_->IncWindowUpdateWrites();
  }

  void FlushData() {
    if (!s_->sent_initial_metadata) return;

    if (s_->flow_controlled_buffer.length == 0 &&
        s_->compressed_data_buffer.length == 0) {
      return;  // early out: nothing to do
    }

    DataSendContext data_send_context(write_context_, t_, s_);

    if (!data_send_context.AnyOutgoing()) {
      if (t_->flow_control->remote_window() <= 0) {
        report_stall(t_, s_, "transport");
        grpc_chttp2_list_add_stalled_by_transport(t_, s_);
      } else if (data_send_context.stream_remote_window() <= 0) {
        report_stall(t_, s_, "stream");
        grpc_chttp2_list_add_stalled_by_stream(t_, s_);
      }
      return;  // early out: nothing to do
    }

    while ((s_->flow_controlled_buffer.length > 0 ||
            s_->compressed_data_buffer.length > 0) &&
           data_send_context.max_outgoing() > 0) {
      if (s_->compressed_data_buffer.length > 0) {
        data_send_context.FlushCompressedBytes();
      } else {
        data_send_context.CompressMoreBytes();
      }
    }
    write_context_->ResetPingRecvClock();
    if (data_send_context.is_last_frame()) {
      SentLastFrame();
    }
    data_send_context.CallCallbacks();
    stream_became_writable_ = true;
    if (s_->flow_controlled_buffer.length > 0 ||
        s_->compressed_data_buffer.length > 0) {
      GRPC_CHTTP2_STREAM_REF(s_, "chttp2_writing:fork");
      grpc_chttp2_list_add_writable_stream(t_, s_);
    }
    write_context_->IncMessageWrites();
  }

  void FlushTrailingMetadata() {
    if (!s_->sent_initial_metadata) return;

    if (s_->send_trailing_metadata == nullptr) return;
    if (s_->fetching_send_message != nullptr) return;
    if (s_->flow_controlled_buffer.length != 0) return;
    if (s_->compressed_data_buffer.length != 0) return;

    GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "sending trailing_metadata"));
    if (grpc_metadata_batch_is_empty(s_->send_trailing_metadata)) {
      grpc_chttp2_encode_data(s_->id, &s_->flow_controlled_buffer, 0, true,
                              &s_->stats.outgoing, &t_->outbuf);
    } else {
      grpc_encode_header_options hopt = {
          s_->id, true,
          t_->settings[GRPC_PEER_SETTINGS]
                      [GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA] !=
              0,

          t_->settings[GRPC_PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE],
          &s_->stats.outgoing};
      grpc_chttp2_encode_header(&t_->hpack_compressor,
                                extra_headers_for_trailing_metadata_,
                                num_extra_headers_for_trailing_metadata_,
                                s_->send_trailing_metadata, &hopt, &t_->outbuf);
    }
    write_context_->IncTrailingMetadataWrites();
    write_context_->ResetPingRecvClock();
    SentLastFrame();

    write_context_->NoteScheduledResults();
    grpc_chttp2_complete_closure_step(
        t_, s_, &s_->send_trailing_metadata_finished, GRPC_ERROR_NONE,
        "send_trailing_metadata_finished");
  }

  bool stream_became_writable() { return stream_became_writable_; }

 private:
  void ConvertInitialMetadataToTrailingMetadata() {
    GRPC_CHTTP2_IF_TRACING(
        gpr_log(GPR_INFO, "not sending initial_metadata (Trailers-Only)"));
    // When sending Trailers-Only, we need to move the :status and
    // content-type headers to the trailers.
    if (s_->send_initial_metadata->idx.named.status != nullptr) {
      extra_headers_for_trailing_metadata_
          [num_extra_headers_for_trailing_metadata_++] =
              &s_->send_initial_metadata->idx.named.status->md;
    }
    if (s_->send_initial_metadata->idx.named.content_type != nullptr) {
      extra_headers_for_trailing_metadata_
          [num_extra_headers_for_trailing_metadata_++] =
              &s_->send_initial_metadata->idx.named.content_type->md;
    }
  }

  void SentLastFrame() {
    s_->send_trailing_metadata = nullptr;
    s_->sent_trailing_metadata = true;

    if (!t_->is_client && !s_->read_closed) {
      grpc_slice_buffer_add(
          &t_->outbuf, grpc_chttp2_rst_stream_create(
                           s_->id, GRPC_HTTP2_NO_ERROR, &s_->stats.outgoing));
    }
    grpc_chttp2_mark_stream_closed(t_, s_, !t_->is_client, true,
                                   GRPC_ERROR_NONE);
  }

  WriteContext* const write_context_;
  grpc_chttp2_transport* const t_;
  grpc_chttp2_stream* const s_;
  bool stream_became_writable_ = false;
  grpc_mdelem* extra_headers_for_trailing_metadata_[2];
  size_t num_extra_headers_for_trailing_metadata_ = 0;
};
}  // namespace

grpc_chttp2_begin_write_result grpc_chttp2_begin_write(
    grpc_chttp2_transport* t) {
  WriteContext ctx(t);
  ctx.FlushSettings();
  ctx.FlushPingAcks();
  ctx.FlushQueuedBuffers();
  ctx.EnactHpackSettings();

  if (t->flow_control->remote_window() > 0) {
    ctx.UpdateStreamsNoLongerStalled();
  }

  /* for each grpc_chttp2_stream that's become writable, frame it's data
     (according to available window sizes) and add to the output buffer */
  while (grpc_chttp2_stream* s = ctx.NextStream()) {
    StreamWriteContext stream_ctx(&ctx, s);
    stream_ctx.FlushInitialMetadata();
    stream_ctx.FlushWindowUpdates();
    stream_ctx.FlushData();
    stream_ctx.FlushTrailingMetadata();

    if (stream_ctx.stream_became_writable()) {
      if (!grpc_chttp2_list_add_writing_stream(t, s)) {
        /* already in writing list: drop ref */
        GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:already_writing");
      } else {
        /* ref will be dropped at end of write */
      }
    } else {
      GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:no_write");
    }
  }

  ctx.FlushWindowUpdates();

  maybe_initiate_ping(t);

  return ctx.Result();
}

void grpc_chttp2_end_write(grpc_chttp2_transport* t, grpc_error* error) {
  GPR_TIMER_SCOPE("grpc_chttp2_end_write", 0);
  grpc_chttp2_stream* s;

  while (grpc_chttp2_list_pop_writing_stream(t, &s)) {
    if (s->sending_bytes != 0) {
      update_list(t, s, static_cast<int64_t>(s->sending_bytes),
                  &s->on_write_finished_cbs, &s->flow_controlled_bytes_written,
                  GRPC_ERROR_REF(error));
      s->sending_bytes = 0;
    }
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:end");
  }
  grpc_slice_buffer_reset_and_unref_internal(&t->outbuf);
  GRPC_ERROR_UNREF(error);
}
