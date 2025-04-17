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

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <stddef.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/ext/transport/chttp2/transport/stream_lists.h"
#include "src/core/ext/transport/chttp2/transport/write_size_policy.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/context_list_entry.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"

// IWYU pragma: no_include "src/core/util/orphanable.h"

using grpc_core::http2::Http2ErrorCode;

static void add_to_write_list(grpc_chttp2_write_cb** list,
                              grpc_chttp2_write_cb* cb) {
  cb->next = *list;
  *list = cb;
}

static void finish_write_cb(grpc_chttp2_transport* t, grpc_chttp2_write_cb* cb,
                            grpc_error_handle error) {
  grpc_chttp2_complete_closure_step(t, &cb->closure, error, "finish_write_cb");
  cb->next = t->write_cb_pool;
  t->write_cb_pool = cb;
}

static grpc_core::Duration NextAllowedPingInterval(grpc_chttp2_transport* t) {
  if (t->is_client) {
    return (t->keepalive_permit_without_calls == 0 && t->stream_map.empty())
               ? grpc_core::Duration::Hours(2)
               : grpc_core::Duration::Seconds(
                     1);  // A second is added to deal with
                          // network delays and timing imprecision
  }
  if (t->sent_goaway_state != GRPC_CHTTP2_GRACEFUL_GOAWAY) {
    // The gRPC keepalive spec doesn't call for any throttling on the server
    // side, but we are adding some throttling for protection anyway, unless
    // we are doing a graceful GOAWAY in which case we don't want to wait.
    if (grpc_core::IsMultipingEnabled()) {
      return grpc_core::Duration::Seconds(1);
    }
    return t->keepalive_time == grpc_core::Duration::Infinity()
               ? grpc_core::Duration::Seconds(20)
               : t->keepalive_time / 2;
  }
  return grpc_core::Duration::Zero();
}

static void maybe_initiate_ping(grpc_chttp2_transport* t) {
  if (!t->ping_callbacks.ping_requested()) {
    // no ping needed: wait
    return;
  }
  // InvalidateNow to avoid getting stuck re-initializing the ping timer
  // in a loop while draining the currently-held combiner. Also see
  // https://github.com/grpc/grpc/issues/26079.
  grpc_core::ExecCtx::Get()->InvalidateNow();
  Match(
      t->ping_rate_policy.RequestSendPing(NextAllowedPingInterval(t),
                                          t->ping_callbacks.pings_inflight()),
      [t](grpc_core::Chttp2PingRatePolicy::SendGranted) {
        t->ping_rate_policy.SentPing();
        grpc_core::SharedBitGen g;
        const uint64_t id = t->ping_callbacks.StartPing(g);
        t->http2_ztrace_collector.Append(
            grpc_core::H2PingTrace<false>{false, id});
        grpc_slice_buffer_add(t->outbuf.c_slice_buffer(),
                              grpc_chttp2_ping_create(false, id));
        t->keepalive_incoming_data_wanted = true;
        if (t->channelz_socket != nullptr) {
          t->channelz_socket->RecordKeepaliveSent();
        }
        grpc_core::global_stats().IncrementHttp2PingsSent();
        if (GRPC_TRACE_FLAG_ENABLED(http) ||
            GRPC_TRACE_FLAG_ENABLED(bdp_estimator) ||
            GRPC_TRACE_FLAG_ENABLED(http_keepalive) ||
            GRPC_TRACE_FLAG_ENABLED(http2_ping)) {
          LOG(INFO) << (t->is_client ? "CLIENT" : "SERVER") << "[" << t
                    << "]: Ping " << id << " sent ["
                    << std::string(t->peer_string.as_string_view())
                    << "]: " << t->ping_rate_policy.GetDebugString();
        }
      },
      [t](grpc_core::Chttp2PingRatePolicy::TooManyRecentPings) {
        // need to receive something of substance before sending a ping again
        if (GRPC_TRACE_FLAG_ENABLED(http) ||
            GRPC_TRACE_FLAG_ENABLED(bdp_estimator) ||
            GRPC_TRACE_FLAG_ENABLED(http_keepalive) ||
            GRPC_TRACE_FLAG_ENABLED(http2_ping)) {
          LOG(INFO) << (t->is_client ? "CLIENT" : "SERVER") << "[" << t
                    << "]: Ping delayed ["
                    << std::string(t->peer_string.as_string_view())
                    << "]: too many recent pings: "
                    << t->ping_rate_policy.GetDebugString();
        }
      },
      [t](grpc_core::Chttp2PingRatePolicy::TooSoon too_soon) {
        // not enough elapsed time between successive pings
        if (GRPC_TRACE_FLAG_ENABLED(http) ||
            GRPC_TRACE_FLAG_ENABLED(bdp_estimator) ||
            GRPC_TRACE_FLAG_ENABLED(http_keepalive) ||
            GRPC_TRACE_FLAG_ENABLED(http2_ping)) {
          LOG(INFO) << (t->is_client ? "CLIENT" : "SERVER") << "[" << t
                    << "]: Ping delayed ["
                    << std::string(t->peer_string.as_string_view())
                    << "]: not enough time elapsed since last "
                       "ping. Last ping:"
                    << too_soon.last_ping
                    << ", minimum wait:" << too_soon.next_allowed_ping_interval
                    << ", need to wait:" << too_soon.wait;
        }
        if (t->delayed_ping_timer_handle ==
            grpc_event_engine::experimental::EventEngine::TaskHandle::
                kInvalid) {
          t->delayed_ping_timer_handle = t->event_engine->RunAfter(
              too_soon.wait, [t = t->Ref()]() mutable {
                grpc_core::ExecCtx exec_ctx;
                grpc_chttp2_retry_initiate_ping(std::move(t));
              });
        }
      });
}

static bool update_list(grpc_chttp2_transport* t, int64_t send_bytes,
                        grpc_chttp2_write_cb** list, int64_t* ctr,
                        grpc_error_handle error) {
  bool sched_any = false;
  grpc_chttp2_write_cb* cb = *list;
  *list = nullptr;
  *ctr += send_bytes;
  while (cb) {
    grpc_chttp2_write_cb* next = cb->next;
    if (cb->call_at_byte <= *ctr) {
      sched_any = true;
      finish_write_cb(t, cb, error);
    } else {
      add_to_write_list(list, cb);
    }
    cb = next;
  }
  return sched_any;
}

static void report_stall(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                         const char* staller) {
  GRPC_TRACE_VLOG(flowctl, 2)
      << t->peer_string.as_string_view() << ":" << t << " stream " << s->id
      << " moved to stalled list by " << staller
      << ". This is FULLY expected to happen in a healthy program that is not "
         "seeing flow control stalls. However, if you know that there are "
         "unwanted stalls, here is some helpful data: [fc:pending="
      << s->flow_controlled_buffer.length
      << ":flowed=" << s->flow_controlled_bytes_flowed
      << ":peer_initwin=" << t->settings.acked().initial_window_size()
      << ":t_win=" << t->flow_control.remote_window() << ":s_win="
      << static_cast<uint32_t>(std::max(
             int64_t{0}, s->flow_control.remote_window_delta() +
                             static_cast<int64_t>(
                                 t->settings.peer().initial_window_size())))
      << ":s_delta=" << s->flow_control.remote_window_delta() << "]";
}

namespace {

class CountDefaultMetadataEncoder {
 public:
  size_t count() const { return count_; }

  void Encode(const grpc_core::Slice&, const grpc_core::Slice&) {}

  template <typename Which>
  void Encode(Which, const typename Which::ValueType&) {
    count_++;
  }

 private:
  size_t count_ = 0;
};

}  // namespace

// Returns true if initial_metadata contains only default headers.
static bool is_default_initial_metadata(grpc_metadata_batch* initial_metadata) {
  CountDefaultMetadataEncoder enc;
  initial_metadata->Encode(&enc);
  return enc.count() == initial_metadata->count();
}

namespace {

class WriteContext {
 public:
  explicit WriteContext(grpc_chttp2_transport* t) : t_(t) {
    t->http2_stats.IncrementHttp2WritesBegun();
    t->http2_stats.IncrementHttp2WriteTargetSize(target_write_size_);
  }

  void FlushSettings() {
    auto update = t_->settings.MaybeSendUpdate();
    if (update.has_value()) {
      t_->http2_ztrace_collector.Append([&update]() {
        return grpc_core::H2SettingsTrace<false>{false, update->settings};
      });
      grpc_core::Http2Frame frame(std::move(*update));
      Serialize(absl::Span<grpc_core::Http2Frame>(&frame, 1), t_->outbuf);
      if (t_->keepalive_timeout != grpc_core::Duration::Infinity()) {
        CHECK(
            t_->settings_ack_watchdog ==
            grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid);
        // We base settings timeout on keepalive timeout, but double it to allow
        // for implementations taking some more time about acking a setting.
        t_->settings_ack_watchdog = t_->event_engine->RunAfter(
            t_->settings_timeout, [t = t_->Ref()]() mutable {
              grpc_core::ExecCtx exec_ctx;
              grpc_chttp2_settings_timeout(std::move(t));
            });
      }
      t_->flow_control.FlushedSettings();
      grpc_core::global_stats().IncrementHttp2SettingsWrites();
    }
  }

  void FlushQueuedBuffers() {
    // simple writes are queued to qbuf, and flushed here
    grpc_slice_buffer_move_into(&t_->qbuf, t_->outbuf.c_slice_buffer());
    t_->num_pending_induced_frames = 0;
    CHECK_EQ(t_->qbuf.count, 0u);
  }

  void FlushWindowUpdates() {
    uint32_t transport_announce = t_->flow_control.MaybeSendUpdate(
        t_->outbuf.c_slice_buffer()->count > 0);
    if (transport_announce) {
      t_->http2_ztrace_collector.Append(
          grpc_core::H2WindowUpdateTrace<false>{0, transport_announce});
      grpc_slice_buffer_add(
          t_->outbuf.c_slice_buffer(),
          grpc_chttp2_window_update_create(0, transport_announce, nullptr));
      grpc_chttp2_reset_ping_clock(t_);
    }
  }

  void FlushPingAcks() {
    if (t_->ping_ack_count == 0) return;
    // Limit the size of writes if we include ping acks - to avoid the ack being
    // delayed by crypto operations.
    target_write_size_ = 0;
    for (size_t i = 0; i < t_->ping_ack_count; i++) {
      t_->http2_ztrace_collector.Append(
          grpc_core::H2PingTrace<false>{true, t_->ping_acks[i]});
      grpc_slice_buffer_add(t_->outbuf.c_slice_buffer(),
                            grpc_chttp2_ping_create(true, t_->ping_acks[i]));
    }
    t_->ping_ack_count = 0;
  }

  void EnactHpackSettings() {
    t_->hpack_compressor.SetMaxTableSize(
        t_->settings.peer().header_table_size());
  }

  void UpdateStreamsNoLongerStalled() {
    grpc_chttp2_stream* s;
    while (grpc_chttp2_list_pop_stalled_by_transport(t_, &s)) {
      if (t_->closed_with_error.ok() &&
          grpc_chttp2_list_add_writable_stream(t_, s)) {
        if (!s->refcount->refs.RefIfNonZero()) {
          grpc_chttp2_list_remove_writable_stream(t_, s);
        }
      }
    }
  }

  grpc_chttp2_stream* NextStream() {
    if (t_->outbuf.c_slice_buffer()->length > target_write_size_) {
      result_.partial = true;
      return nullptr;
    }

    grpc_chttp2_stream* s;
    if (!grpc_chttp2_list_pop_writable_stream(t_, &s)) {
      return nullptr;
    }

    return s;
  }

  void IncInitialMetadataWrites() { ++initial_metadata_writes_; }
  void IncWindowUpdateWrites() { ++flow_control_writes_; }
  void IncMessageWrites() { ++message_writes_; }
  void IncTrailingMetadataWrites() { ++trailing_metadata_writes_; }

  void NoteScheduledResults() { result_.early_results_scheduled = true; }

  grpc_chttp2_transport* transport() const { return t_; }

  grpc_chttp2_begin_write_result Result() {
    result_.writing = t_->outbuf.c_slice_buffer()->count > 0;
    return result_;
  }

  size_t target_write_size() const { return target_write_size_; }

 private:
  grpc_chttp2_transport* const t_;
  size_t target_write_size_ = t_->write_size_policy.WriteTargetSize();

  // stats histogram counters: we increment these throughout this function,
  // and at the end publish to the central stats histograms
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
    return static_cast<uint32_t>(std::max(
        int64_t{0},
        s_->flow_control.remote_window_delta() +
            static_cast<int64_t>(t_->settings.peer().initial_window_size())));
  }

  uint32_t max_outgoing() const {
    return grpc_core::Clamp<uint32_t>(
        std::min<int64_t>(
            {t_->settings.peer().max_frame_size(), stream_remote_window(),
             t_->flow_control.remote_window(),
             static_cast<int64_t>(write_context_->target_write_size())}),
        0, std::numeric_limits<uint32_t>::max());
  }

  bool AnyOutgoing() const { return max_outgoing() > 0; }

  void FlushBytes() {
    uint32_t send_bytes =
        static_cast<uint32_t>(std::min(static_cast<size_t>(max_outgoing()),
                                       s_->flow_controlled_buffer.length));
    is_last_frame_ = send_bytes == s_->flow_controlled_buffer.length &&
                     s_->send_trailing_metadata != nullptr &&
                     s_->send_trailing_metadata->empty();
    grpc_chttp2_encode_data(s_->id, &s_->flow_controlled_buffer, send_bytes,
                            is_last_frame_, &s_->call_tracer_wrapper,
                            &t_->http2_ztrace_collector,
                            t_->outbuf.c_slice_buffer());
    sfc_upd_.SentData(send_bytes);
    s_->sending_bytes += send_bytes;
  }

  bool is_last_frame() const { return is_last_frame_; }

  void CallCallbacks() {
    if (update_list(
            t_, static_cast<int64_t>(s_->sending_bytes - sending_bytes_before_),
            &s_->on_flow_controlled_cbs, &s_->flow_controlled_bytes_flowed,
            absl::OkStatus())) {
      write_context_->NoteScheduledResults();
    }
  }

 private:
  WriteContext* write_context_;
  grpc_chttp2_transport* t_;
  grpc_chttp2_stream* s_;
  grpc_core::chttp2::StreamFlowControl::OutgoingUpdateContext sfc_upd_{
      &s_->flow_control};
  const size_t sending_bytes_before_;
  bool is_last_frame_ = false;
};

class StreamWriteContext {
 public:
  StreamWriteContext(WriteContext* write_context, grpc_chttp2_stream* s)
      : write_context_(write_context), t_(write_context->transport()), s_(s) {
    GRPC_CHTTP2_IF_TRACING(INFO)
        << "W:" << t_ << " " << (t_->is_client ? "CLIENT" : "SERVER") << "["
        << s->id << "] im-(sent,send)=(" << s->sent_initial_metadata << ","
        << (s->send_initial_metadata != nullptr) << ")";
  }

  void FlushInitialMetadata() {
    // send initial metadata if it's available
    if (s_->sent_initial_metadata) return;
    if (s_->send_initial_metadata == nullptr) return;

    // We skip this on the server side if there is no custom initial
    // metadata, there are no messages to send, and we are also sending
    // trailing metadata.  This results in a Trailers-Only response,
    // which is required for retries, as per:
    // https://github.com/grpc/proposal/blob/master/A6-client-retries.md#when-retries-are-valid
    if (!t_->is_client && s_->flow_controlled_buffer.length == 0 &&
        s_->send_trailing_metadata != nullptr &&
        is_default_initial_metadata(s_->send_initial_metadata)) {
      ConvertInitialMetadataToTrailingMetadata();
    } else {
      t_->hpack_compressor.EncodeHeaders(
          grpc_core::HPackCompressor::EncodeHeaderOptions{
              s_->id,  // stream_id
              false,   // is_eof
              t_->settings.peer()
                  .allow_true_binary_metadata(),     // use_true_binary_metadata
              t_->settings.peer().max_frame_size(),  // max_frame_size
              &s_->call_tracer_wrapper, &t_->http2_ztrace_collector},
          *s_->send_initial_metadata, t_->outbuf.c_slice_buffer());
      grpc_chttp2_reset_ping_clock(t_);
      write_context_->IncInitialMetadataWrites();
    }

    s_->send_initial_metadata = nullptr;
    s_->sent_initial_metadata = true;
    write_context_->NoteScheduledResults();
    grpc_chttp2_complete_closure_step(t_, &s_->send_initial_metadata_finished,
                                      absl::OkStatus(),
                                      "send_initial_metadata_finished");
    if (!grpc_core::IsCallTracerTransportFixEnabled()) {
      if (s_->parent_call_tracer != nullptr) {
        grpc_core::HttpAnnotation::WriteStats write_stats;
        write_stats.target_write_size = write_context_->target_write_size();
        s_->parent_call_tracer->RecordAnnotation(
            grpc_core::HttpAnnotation(
                grpc_core::HttpAnnotation::Type::kHeadWritten,
                gpr_now(GPR_CLOCK_REALTIME))
                .Add(s_->t->flow_control.stats())
                .Add(s_->flow_control.stats())
                .Add(write_stats));
      }
    } else {
      if (s_->call_tracer != nullptr && s_->call_tracer->IsSampled()) {
        grpc_core::HttpAnnotation::WriteStats write_stats;
        write_stats.target_write_size = write_context_->target_write_size();
        s_->call_tracer->RecordAnnotation(
            grpc_core::HttpAnnotation(
                grpc_core::HttpAnnotation::Type::kHeadWritten,
                gpr_now(GPR_CLOCK_REALTIME))
                .Add(s_->t->flow_control.stats())
                .Add(s_->flow_control.stats())
                .Add(write_stats));
      }
    }
  }

  void FlushWindowUpdates() {
    if (s_->read_closed) return;

    // send any window updates
    const uint32_t stream_announce = s_->flow_control.MaybeSendUpdate();
    if (stream_announce == 0) return;

    t_->http2_ztrace_collector.Append(
        grpc_core::H2WindowUpdateTrace<false>{s_->id, stream_announce});
    grpc_slice_buffer_add(
        t_->outbuf.c_slice_buffer(),
        grpc_chttp2_window_update_create(s_->id, stream_announce,
                                         &s_->call_tracer_wrapper));
    grpc_chttp2_reset_ping_clock(t_);
    write_context_->IncWindowUpdateWrites();
  }

  void FlushData() {
    if (!s_->sent_initial_metadata) return;

    if (s_->flow_controlled_buffer.length == 0) {
      return;  // early out: nothing to do
    }

    DataSendContext data_send_context(write_context_, t_, s_);

    if (!data_send_context.AnyOutgoing()) {
      if (t_->flow_control.remote_window() <= 0) {
        t_->http2_ztrace_collector.Append(grpc_core::H2FlowControlStall{
            t_->flow_control.remote_window(),
            data_send_context.stream_remote_window(), s_->id});
        grpc_core::global_stats().IncrementHttp2TransportStalls();
        report_stall(t_, s_, "transport");
        grpc_chttp2_list_add_stalled_by_transport(t_, s_);
      } else if (data_send_context.stream_remote_window() <= 0) {
        t_->http2_ztrace_collector.Append(grpc_core::H2FlowControlStall{
            t_->flow_control.remote_window(),
            data_send_context.stream_remote_window(), s_->id});
        grpc_core::global_stats().IncrementHttp2StreamStalls();
        report_stall(t_, s_, "stream");
        grpc_chttp2_list_add_stalled_by_stream(t_, s_);
      }
      return;  // early out: nothing to do
    }

    while (s_->flow_controlled_buffer.length > 0 &&
           data_send_context.max_outgoing() > 0) {
      data_send_context.FlushBytes();
    }
    grpc_chttp2_reset_ping_clock(t_);
    if (data_send_context.is_last_frame()) {
      SentLastFrame();
    }
    data_send_context.CallCallbacks();
    stream_became_writable_ = true;
    if (s_->flow_controlled_buffer.length > 0) {
      GRPC_CHTTP2_STREAM_REF(s_, "chttp2_writing:fork");
      grpc_chttp2_list_add_writable_stream(t_, s_);
    }
    write_context_->IncMessageWrites();
  }

  void FlushTrailingMetadata() {
    if (!s_->sent_initial_metadata) return;

    if (s_->send_trailing_metadata == nullptr) return;
    if (s_->flow_controlled_buffer.length != 0) return;

    GRPC_CHTTP2_IF_TRACING(INFO) << "sending trailing_metadata";
    if (s_->send_trailing_metadata->empty()) {
      grpc_chttp2_encode_data(s_->id, &s_->flow_controlled_buffer, 0, true,
                              &s_->call_tracer_wrapper,
                              &t_->http2_ztrace_collector,
                              t_->outbuf.c_slice_buffer());
    } else {
      t_->hpack_compressor.EncodeHeaders(
          grpc_core::HPackCompressor::EncodeHeaderOptions{
              s_->id, true, t_->settings.peer().allow_true_binary_metadata(),
              t_->settings.peer().max_frame_size(), &s_->call_tracer_wrapper,
              &t_->http2_ztrace_collector},
          *s_->send_trailing_metadata, t_->outbuf.c_slice_buffer());
    }
    write_context_->IncTrailingMetadataWrites();
    grpc_chttp2_reset_ping_clock(t_);
    SentLastFrame();

    write_context_->NoteScheduledResults();
    grpc_chttp2_complete_closure_step(t_, &s_->send_trailing_metadata_finished,
                                      absl::OkStatus(),
                                      "send_trailing_metadata_finished");
  }

  bool stream_became_writable() { return stream_became_writable_; }

 private:
  class TrailersOnlyMetadataEncoder {
   public:
    explicit TrailersOnlyMetadataEncoder(grpc_metadata_batch* trailing_md)
        : trailing_md_(trailing_md) {}

    template <typename Which, typename Value>
    void Encode(Which which, Value value) {
      if (Which::kTransferOnTrailersOnly) {
        trailing_md_->Set(which, value);
      }
    }

    template <typename Which>
    void Encode(Which which, const grpc_core::Slice& value) {
      if (Which::kTransferOnTrailersOnly) {
        trailing_md_->Set(which, value.Ref());
      }
    }

    // Non-grpc metadata should not be transferred.
    void Encode(const grpc_core::Slice&, const grpc_core::Slice&) {}

   private:
    grpc_metadata_batch* trailing_md_;
  };

  void ConvertInitialMetadataToTrailingMetadata() {
    GRPC_CHTTP2_IF_TRACING(INFO)
        << "not sending initial_metadata (Trailers-Only)";
    // When sending Trailers-Only, we need to move metadata from headers to
    // trailers.
    TrailersOnlyMetadataEncoder encoder(s_->send_trailing_metadata);
    s_->send_initial_metadata->Encode(&encoder);
  }

  void SentLastFrame() {
    s_->send_trailing_metadata = nullptr;
    if (s_->sent_trailing_metadata_op) {
      *s_->sent_trailing_metadata_op = true;
      s_->sent_trailing_metadata_op = nullptr;
    }
    s_->sent_trailing_metadata = true;
    s_->eos_sent = true;

    if (!t_->is_client && !s_->read_closed) {
      grpc_slice_buffer_add(
          t_->outbuf.c_slice_buffer(),
          grpc_chttp2_rst_stream_create(
              s_->id, static_cast<uint32_t>(Http2ErrorCode::kNoError),
              &s_->call_tracer_wrapper, &t_->http2_ztrace_collector));
    }
    grpc_chttp2_mark_stream_closed(t_, s_, !t_->is_client, true,
                                   absl::OkStatus());
    if (!grpc_core::IsCallTracerTransportFixEnabled()) {
      if (s_->parent_call_tracer != nullptr) {
        s_->parent_call_tracer->RecordAnnotation(
            grpc_core::HttpAnnotation(grpc_core::HttpAnnotation::Type::kEnd,
                                      gpr_now(GPR_CLOCK_REALTIME))
                .Add(s_->t->flow_control.stats())
                .Add(s_->flow_control.stats()));
      }
    } else {
      if (s_->call_tracer != nullptr && s_->call_tracer->IsSampled()) {
        s_->call_tracer->RecordAnnotation(
            grpc_core::HttpAnnotation(grpc_core::HttpAnnotation::Type::kEnd,
                                      gpr_now(GPR_CLOCK_REALTIME))
                .Add(s_->t->flow_control.stats())
                .Add(s_->flow_control.stats()));
      }
    }
  }

  WriteContext* const write_context_;
  grpc_chttp2_transport* const t_;
  grpc_chttp2_stream* const s_;
  bool stream_became_writable_ = false;
};
}  // namespace

grpc_chttp2_begin_write_result grpc_chttp2_begin_write(
    grpc_chttp2_transport* t) {
  GRPC_LATENT_SEE_INNER_SCOPE("grpc_chttp2_begin_write");

  int64_t outbuf_relative_start_pos = 0;
  WriteContext ctx(t);

  t->http2_ztrace_collector.Append(grpc_core::H2BeginWriteCycle{
      static_cast<uint32_t>(ctx.target_write_size())});

  ctx.FlushSettings();
  ctx.FlushPingAcks();
  ctx.FlushQueuedBuffers();
  ctx.EnactHpackSettings();

  if (t->flow_control.remote_window() > 0) {
    ctx.UpdateStreamsNoLongerStalled();
  }

  // for each grpc_chttp2_stream that's become writable, frame it's data
  // (according to available window sizes) and add to the output buffer
  while (grpc_chttp2_stream* s = ctx.NextStream()) {
    StreamWriteContext stream_ctx(&ctx, s);
    size_t orig_len = t->outbuf.c_slice_buffer()->length;
    int64_t num_stream_bytes = 0;
    stream_ctx.FlushInitialMetadata();
    stream_ctx.FlushWindowUpdates();
    stream_ctx.FlushData();
    stream_ctx.FlushTrailingMetadata();
    if (t->outbuf.c_slice_buffer()->length > orig_len) {
      // Add this stream to the list of the contexts to be traced at TCP
      num_stream_bytes = t->outbuf.c_slice_buffer()->length - orig_len;
      s->byte_counter += static_cast<size_t>(num_stream_bytes);
      ++s->write_counter;
      if (s->traced && grpc_endpoint_can_track_err(t->ep.get())) {
        grpc_core::CopyContextFn copy_context_fn =
            grpc_core::GrpcHttp2GetCopyContextFn();
        if (copy_context_fn != nullptr &&
            grpc_core::GrpcHttp2GetWriteTimestampsCallback() != nullptr) {
          t->context_list->emplace_back(
              copy_context_fn(s->arena), outbuf_relative_start_pos,
              num_stream_bytes, s->byte_counter, s->write_counter - 1, nullptr);
        }
      }
      outbuf_relative_start_pos += num_stream_bytes;
    }
    if (stream_ctx.stream_became_writable()) {
      if (!grpc_chttp2_list_add_writing_stream(t, s)) {
        // already in writing list: drop ref
        GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:already_writing");
      } else {
        // ref will be dropped at end of write
      }
    } else {
      GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:no_write");
    }
  }

  ctx.FlushWindowUpdates();

  maybe_initiate_ping(t);

  t->write_flow.Begin(GRPC_LATENT_SEE_METADATA("write"));

  return ctx.Result();
}

void grpc_chttp2_end_write(grpc_chttp2_transport* t, grpc_error_handle error) {
  GRPC_LATENT_SEE_INNER_SCOPE("grpc_chttp2_end_write");
  grpc_chttp2_stream* s;

  t->write_flow.End();
  t->http2_ztrace_collector.Append(grpc_core::H2EndWriteCycle{});

  if (t->channelz_socket != nullptr) {
    t->channelz_socket->RecordMessagesSent(t->num_messages_in_next_write);
  }
  t->num_messages_in_next_write = 0;

  if (t->ping_callbacks.started_new_ping_without_setting_timeout() &&
      t->keepalive_timeout != grpc_core::Duration::Infinity()) {
    // Set ping timeout after finishing write so we don't measure our own send
    // time.
    const auto timeout = t->ping_timeout;
    auto id = t->ping_callbacks.OnPingTimeout(timeout, t->event_engine.get(),
                                              [t = t->Ref()] {
                                                grpc_core::ExecCtx exec_ctx;
                                                grpc_chttp2_ping_timeout(t);
                                              });
    if (GRPC_TRACE_FLAG_ENABLED(http2_ping) && id.has_value()) {
      LOG(INFO) << (t->is_client ? "CLIENT" : "SERVER") << "[" << t
                << "]: Set ping timeout timer of " << timeout.ToString()
                << " for ping id " << id.value();
    }

    if (t->keepalive_incoming_data_wanted &&
        t->keepalive_timeout < t->ping_timeout &&
        t->keepalive_ping_timeout_handle !=
            grpc_event_engine::experimental::EventEngine::TaskHandle::
                kInvalid) {
      if (GRPC_TRACE_FLAG_ENABLED(http2_ping) ||
          GRPC_TRACE_FLAG_ENABLED(http_keepalive)) {
        LOG(INFO) << (t->is_client ? "CLIENT" : "SERVER") << "[" << t
                  << "]: Set keepalive ping timeout timer of "
                  << t->keepalive_timeout.ToString();
      }
      t->keepalive_ping_timeout_handle =
          t->event_engine->RunAfter(t->keepalive_timeout, [t = t->Ref()] {
            grpc_core::ExecCtx exec_ctx;
            grpc_chttp2_keepalive_timeout(t);
          });
    }
  }

  while (grpc_chttp2_list_pop_writing_stream(t, &s)) {
    if (s->sending_bytes != 0) {
      update_list(t, static_cast<int64_t>(s->sending_bytes),
                  &s->on_write_finished_cbs, &s->flow_controlled_bytes_written,
                  error);
      s->sending_bytes = 0;
    }
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:end");
  }
  grpc_slice_buffer_reset_and_unref(t->outbuf.c_slice_buffer());
}
