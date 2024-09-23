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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_TRANSPORT_H

#include <cstdint>
#include <string>

#include "absl/types/optional.h"

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"

struct grpc_chttp2_transport final : public grpc_core::FilterStackTransport,
                                     public grpc_core::KeepsGrpcInitialized {
  grpc_chttp2_transport(const grpc_core::ChannelArgs& channel_args,
                        grpc_core::OrphanablePtr<grpc_endpoint> endpoint,
                        bool is_client);
  ~grpc_chttp2_transport() override;

  void Orphan() override;

  grpc_core::RefCountedPtr<grpc_chttp2_transport> Ref() {
    return grpc_core::FilterStackTransport::RefAsSubclass<
        grpc_chttp2_transport>();
  }

  size_t SizeOfStream() const override;
  bool HackyDisableStreamOpBatchCoalescingInConnectedChannel() const override;
  void PerformStreamOp(grpc_stream* gs,
                       grpc_transport_stream_op_batch* op) override;
  void DestroyStream(grpc_stream* gs,
                     grpc_closure* then_schedule_closure) override;

  grpc_core::FilterStackTransport* filter_stack_transport() override {
    return this;
  }
  grpc_core::ClientTransport* client_transport() override { return nullptr; }
  grpc_core::ServerTransport* server_transport() override { return nullptr; }

  absl::string_view GetTransportName() const override;
  void InitStream(grpc_stream* gs, grpc_stream_refcount* refcount,
                  const void* server_data, grpc_core::Arena* arena) override;
  void SetPollset(grpc_stream* stream, grpc_pollset* pollset) override;
  void SetPollsetSet(grpc_stream* stream,
                     grpc_pollset_set* pollset_set) override;
  void PerformOp(grpc_transport_op* op) override;

  grpc_core::OrphanablePtr<grpc_endpoint> ep;
  grpc_core::Mutex ep_destroy_mu;  // Guards endpoint destruction only.

  grpc_core::Slice peer_string;

  grpc_core::MemoryOwner memory_owner;
  const grpc_core::MemoryAllocator::Reservation self_reservation;
  grpc_core::ReclamationSweep active_reclamation;

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine;
  grpc_core::Combiner* combiner;
  absl::BitGen bitgen;

  // On the client side, when the transport is first created, the
  // endpoint will already have been added to this pollset_set, and it
  // needs to stay there until the notify_on_receive_settings callback
  // is invoked.  After that, the polling will be coordinated via the
  // bind_pollset_set transport op, sent by the subchannel when it
  // starts a connectivity watch.
  grpc_pollset_set* interested_parties_until_recv_settings = nullptr;

  grpc_closure* notify_on_receive_settings = nullptr;
  grpc_closure* notify_on_close = nullptr;

  /// has the upper layer closed the transport?
  grpc_error_handle closed_with_error;

  /// various lists of streams
  grpc_chttp2_stream_list lists[STREAM_LIST_COUNT] = {};

  /// maps stream id to grpc_chttp2_stream objects
  absl::flat_hash_map<uint32_t, grpc_chttp2_stream*> stream_map;
  // Count of streams that should be counted against max concurrent streams but
  // are not in stream_map (due to tarpitting).
  size_t extra_streams = 0;

  class RemovedStreamHandle {
   public:
    RemovedStreamHandle() = default;
    explicit RemovedStreamHandle(
        grpc_core::RefCountedPtr<grpc_chttp2_transport> t)
        : transport_(std::move(t)) {
      ++transport_->extra_streams;
    }
    ~RemovedStreamHandle() {
      if (transport_ != nullptr) {
        --transport_->extra_streams;
      }
    }
    RemovedStreamHandle(const RemovedStreamHandle&) = delete;
    RemovedStreamHandle& operator=(const RemovedStreamHandle&) = delete;
    RemovedStreamHandle(RemovedStreamHandle&&) = default;
    RemovedStreamHandle& operator=(RemovedStreamHandle&&) = default;

   private:
    grpc_core::RefCountedPtr<grpc_chttp2_transport> transport_;
  };

  grpc_closure write_action_begin_locked;
  grpc_closure write_action_end_locked;

  grpc_closure read_action_locked;

  /// incoming read bytes
  grpc_slice_buffer read_buffer;

  /// address to place a newly accepted stream - set and unset by
  /// grpc_chttp2_parsing_accept_stream; used by init_stream to
  /// publish the accepted server stream
  grpc_chttp2_stream** accepting_stream = nullptr;

  // accept stream callback
  void (*accept_stream_cb)(void* user_data, grpc_core::Transport* transport,
                           const void* server_data);
  // registered_method_matcher_cb is called before invoking the recv initial
  // metadata callback.
  void (*registered_method_matcher_cb)(
      void* user_data, grpc_core::ServerMetadata* metadata) = nullptr;
  void* accept_stream_cb_user_data;

  /// connectivity tracking
  grpc_core::ConnectivityStateTracker state_tracker;

  /// data to write now
  grpc_core::SliceBuffer outbuf;
  /// hpack encoding
  grpc_core::HPackCompressor hpack_compressor;

  /// data to write next write
  grpc_slice_buffer qbuf;

  size_t max_requests_per_read;

  /// Set to a grpc_error object if a goaway frame is received. By default, set
  /// to absl::OkStatus()
  grpc_error_handle goaway_error;

  grpc_chttp2_sent_goaway_state sent_goaway_state = GRPC_CHTTP2_NO_GOAWAY_SEND;

  /// settings values
  grpc_core::Http2SettingsManager settings;

  grpc_event_engine::experimental::EventEngine::TaskHandle
      settings_ack_watchdog =
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;

  /// what is the next stream id to be allocated by this peer?
  /// copied to next_stream_id in parsing when parsing commences
  uint32_t next_stream_id = 0;

  /// last new stream id
  uint32_t last_new_stream_id = 0;

  /// Number of incoming streams allowed before a settings ACK is required
  uint32_t num_incoming_streams_before_settings_ack = 0;

  /// ping queues for various ping insertion points
  grpc_core::Chttp2PingAbusePolicy ping_abuse_policy;
  grpc_core::Chttp2PingRatePolicy ping_rate_policy;
  grpc_core::Chttp2PingCallbacks ping_callbacks;
  grpc_event_engine::experimental::EventEngine::TaskHandle
      delayed_ping_timer_handle =
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;
  grpc_closure retry_initiate_ping_locked;

  /// ping acks
  size_t ping_ack_count = 0;
  size_t ping_ack_capacity = 0;
  uint64_t* ping_acks = nullptr;

  /// parser for headers
  grpc_core::HPackParser hpack_parser;
  /// simple one shot parsers
  union {
    grpc_chttp2_window_update_parser window_update;
    grpc_chttp2_settings_parser settings;
    grpc_chttp2_ping_parser ping;
    grpc_chttp2_rst_stream_parser rst_stream;
  } simple;
  /// parser for goaway frames
  grpc_chttp2_goaway_parser goaway_parser;

  grpc_core::chttp2::TransportFlowControl flow_control;
  /// initial window change. This is tracked as we parse settings frames from
  /// the remote peer. If there is a positive delta, then we will make all
  /// streams readable since they may have become unstalled
  int64_t initial_window_update = 0;

  // deframing
  grpc_chttp2_deframe_transport_state deframe_state = GRPC_DTS_CLIENT_PREFIX_0;
  uint8_t incoming_frame_type = 0;
  uint8_t incoming_frame_flags = 0;
  uint8_t header_eof = 0;
  bool is_first_frame = true;
  uint32_t expect_continuation_stream_id = 0;
  uint32_t incoming_frame_size = 0;

  int min_tarpit_duration_ms;
  int max_tarpit_duration_ms;
  bool allow_tarpit;

  grpc_chttp2_stream* incoming_stream = nullptr;
  // active parser
  struct Parser {
    const char* name;
    grpc_error_handle (*parser)(void* parser_user_data,
                                grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                                const grpc_slice& slice, int is_last);
    void* user_data = nullptr;
  };
  Parser parser;

  grpc_chttp2_write_cb* write_cb_pool = nullptr;

  // bdp estimator
  grpc_closure next_bdp_ping_timer_expired_locked;
  grpc_closure start_bdp_ping_locked;
  grpc_closure finish_bdp_ping_locked;

  // if non-NULL, close the transport with this error when writes are finished
  grpc_error_handle close_transport_on_writes_finished;

  // a list of closures to run after writes are finished
  grpc_closure_list run_after_write = GRPC_CLOSURE_LIST_INIT;

  // buffer pool state
  /// benign cleanup closure
  grpc_closure benign_reclaimer_locked;
  /// destructive cleanup closure
  grpc_closure destructive_reclaimer_locked;

  // next bdp ping timer handle
  grpc_event_engine::experimental::EventEngine::TaskHandle
      next_bdp_ping_timer_handle =
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;

  // keep-alive ping support
  /// Closure to initialize a keepalive ping
  grpc_closure init_keepalive_ping_locked;
  /// Closure to run when the keepalive ping ack is received
  grpc_closure finish_keepalive_ping_locked;
  /// timer to initiate ping events
  grpc_event_engine::experimental::EventEngine::TaskHandle
      keepalive_ping_timer_handle =
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;
  ;
  /// time duration in between pings
  grpc_core::Duration keepalive_time;
  /// grace period to wait for data after sending a ping before keepalives
  /// timeout
  grpc_core::Duration keepalive_timeout;
  /// number of stream objects currently allocated by this transport
  std::atomic<size_t> streams_allocated{0};
  /// keep-alive state machine state
  grpc_chttp2_keepalive_state keepalive_state;
  // Soft limit on max header size.
  uint32_t max_header_list_size_soft_limit = 0;
  grpc_core::ContextList* context_list = nullptr;
  grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode> channelz_socket;
  uint32_t num_messages_in_next_write = 0;
  /// The number of pending induced frames (SETTINGS_ACK, PINGS_ACK and
  /// RST_STREAM) in the outgoing buffer (t->qbuf). If this number goes beyond
  /// DEFAULT_MAX_PENDING_INDUCED_FRAMES, we pause reading new frames. We would
  /// only continue reading when we are able to write to the socket again,
  /// thereby reducing the number of induced frames.
  uint32_t num_pending_induced_frames = 0;
  uint32_t incoming_stream_id = 0;

  /// grace period after sending a ping to wait for the ping ack
  grpc_core::Duration ping_timeout;
  grpc_event_engine::experimental::EventEngine::TaskHandle
      keepalive_ping_timeout_handle =
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;
  /// grace period before settings timeout expires
  grpc_core::Duration settings_timeout;

  /// how much data are we willing to buffer when the WRITE_BUFFER_HINT is set?
  uint32_t write_buffer_size = grpc_core::chttp2::kDefaultWindow;

  /// write execution state of the transport
  grpc_chttp2_write_state write_state = GRPC_CHTTP2_WRITE_STATE_IDLE;

  /// policy for how much data we're willing to put into one http2 write
  grpc_core::Chttp2WriteSizePolicy write_size_policy;

  bool reading_paused_on_pending_induced_frames = false;
  /// Based on channel args, preferred_rx_crypto_frame_sizes are advertised to
  /// the peer
  bool enable_preferred_rx_crypto_frame_advertisement = false;
  /// Set to non zero if closures associated with the transport may be
  /// covering a write in a pollset. Such closures cannot be scheduled until
  /// we can prove that the write got scheduled.
  uint8_t closure_barrier_may_cover_write = CLOSURE_BARRIER_MAY_COVER_WRITE;

  /// have we scheduled a benign cleanup?
  bool benign_reclaimer_registered = false;
  /// have we scheduled a destructive cleanup?
  bool destructive_reclaimer_registered = false;

  /// if keepalive pings are allowed when there's no outstanding streams
  bool keepalive_permit_without_calls = false;

  // bdp estimator
  bool bdp_ping_blocked =
      false;  // Is the BDP blocked due to not receiving any data?

  /// is the transport destroying itself?
  uint8_t destroying = false;

  /// is this a client?
  bool is_client;

  /// If start_bdp_ping_locked has been called
  bool bdp_ping_started = false;
  // True if pings should be acked
  bool ack_pings = true;
  /// True if the keepalive system wants to see some data incoming
  bool keepalive_incoming_data_wanted = false;
  /// True if we count stream allocation (instead of HTTP2 concurrency) for
  /// MAX_CONCURRENT_STREAMS
  bool max_concurrent_streams_overload_protection = false;

  // What percentage of rst_stream frames on the server should cause a ping
  // frame to be generated.
  uint8_t ping_on_rst_stream_percent;

  GPR_NO_UNIQUE_ADDRESS grpc_core::latent_see::Flow write_flow;
};

grpc_chttp2_transport::RemovedStreamHandle grpc_chttp2_mark_stream_closed(
    grpc_chttp2_transport* t, grpc_chttp2_stream* s, int close_reads,
    int close_writes, grpc_error_handle error);

/// Creates a CHTTP2 Transport. This takes ownership of a \a resource_user ref
/// from the caller; if the caller still needs the resource_user after creating
/// a transport, the caller must take another ref.
grpc_core::Transport* grpc_create_chttp2_transport(
    const grpc_core::ChannelArgs& channel_args,
    grpc_core::OrphanablePtr<grpc_endpoint> ep, bool is_client);

grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode>
grpc_chttp2_transport_get_socket_node(grpc_core::Transport* transport);

/// Takes ownership of \a read_buffer, which (if non-NULL) contains
/// leftover bytes previously read from the endpoint (e.g., by handshakers).
/// If non-null, \a notify_on_receive_settings will be scheduled when
/// HTTP/2 settings are received from the peer.
/// If non-null, the endpoint will be removed from
/// interested_parties_until_recv_settings before
/// notify_on_receive_settings is invoked.
void grpc_chttp2_transport_start_reading(
    grpc_core::Transport* transport, grpc_slice_buffer* read_buffer,
    grpc_closure* notify_on_receive_settings,
    grpc_pollset_set* interested_parties_until_recv_settings,
    grpc_closure* notify_on_close);

namespace grpc_core {
typedef void (*TestOnlyGlobalHttp2TransportInitCallback)();
typedef void (*TestOnlyGlobalHttp2TransportDestructCallback)();

void TestOnlySetGlobalHttp2TransportInitCallback(
    TestOnlyGlobalHttp2TransportInitCallback callback);

void TestOnlySetGlobalHttp2TransportDestructCallback(
    TestOnlyGlobalHttp2TransportDestructCallback callback);

// If \a disable is true, the HTTP2 transport will not update the connectivity
// state tracker to TRANSIENT_FAILURE when a goaway is received. This prevents
// the watchers (eg. client_channel) from noticing the GOAWAY, thereby allowing
// us to test the racy behavior when a call is sent down the stack around the
// same time that a GOAWAY is received.
void TestOnlyGlobalHttp2TransportDisableTransientFailureStateNotification(
    bool disable);

typedef void (*WriteTimestampsCallback)(void*, Timestamps*,
                                        grpc_error_handle error);
typedef void* (*CopyContextFn)(Arena*);

void GrpcHttp2SetWriteTimestampsCallback(WriteTimestampsCallback fn);
void GrpcHttp2SetCopyContextFn(CopyContextFn fn);

WriteTimestampsCallback GrpcHttp2GetWriteTimestampsCallback();
CopyContextFn GrpcHttp2GetCopyContextFn();

// Interprets the passed arg as a ContextList type and for each entry in the
// passed ContextList, it executes the function set using
// GrpcHttp2SetWriteTimestampsCallback method with each context in the list
// and \a ts. It also deletes/frees up the passed ContextList after this
// operation.
void ForEachContextListEntryExecute(void* arg, Timestamps* ts,
                                    grpc_error_handle error);

class HttpAnnotation : public CallTracerAnnotationInterface::Annotation {
 public:
  enum class Type : uint8_t {
    kUnknown = 0,
    // When the first byte enters the HTTP transport.
    kStart,
    // When the first byte leaves the HTTP transport.
    kHeadWritten,
    // When the last byte leaves the HTTP transport.
    kEnd,
  };

  // A snapshot of write stats to export.
  struct WriteStats {
    size_t target_write_size;
  };

  HttpAnnotation(Type type, gpr_timespec time);

  HttpAnnotation& Add(const chttp2::TransportFlowControl::Stats& stats) {
    transport_stats_ = stats;
    return *this;
  }

  HttpAnnotation& Add(const chttp2::StreamFlowControl::Stats& stats) {
    stream_stats_ = stats;
    return *this;
  }

  HttpAnnotation& Add(const WriteStats& stats) {
    write_stats_ = stats;
    return *this;
  }

  std::string ToString() const override;

  Type http_type() const { return type_; }
  gpr_timespec time() const { return time_; }
  absl::optional<chttp2::TransportFlowControl::Stats> transport_stats() const {
    return transport_stats_;
  }
  absl::optional<chttp2::StreamFlowControl::Stats> stream_stats() const {
    return stream_stats_;
  }
  absl::optional<WriteStats> write_stats() const { return write_stats_; }

 private:
  const Type type_;
  const gpr_timespec time_;
  absl::optional<chttp2::TransportFlowControl::Stats> transport_stats_;
  absl::optional<chttp2::StreamFlowControl::Stats> stream_stats_;
  absl::optional<WriteStats> write_stats_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_TRANSPORT_H
