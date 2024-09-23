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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/context_list_entry.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/ext/transport/chttp2/transport/write_size_policy.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/bitset.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"

// Flag that this closure barrier may be covering a write in a pollset, and so
//   we should not complete this closure until we can prove that the write got
//   scheduled
#define CLOSURE_BARRIER_MAY_COVER_WRITE (1 << 0)
// First bit of the reference count, stored in the high order bits (with the low
//   bits being used for flags defined above)
#define CLOSURE_BARRIER_FIRST_REF_BIT (1 << 16)

struct grpc_chttp2_transport;

// streams are kept in various linked lists depending on what things need to
// happen to them... this enum labels each list
typedef enum {
  // If a stream is in the following two lists, an explicit ref is associated
  // with the stream
  GRPC_CHTTP2_LIST_WRITABLE,
  GRPC_CHTTP2_LIST_WRITING,
  // No additional ref is taken for the following refs. Make sure to remove the
  // stream from these lists when the stream is removed.
  GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT,
  GRPC_CHTTP2_LIST_STALLED_BY_STREAM,
  /// streams that are waiting to start because there are too many concurrent
  /// streams on the connection
  GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY,
  STREAM_LIST_COUNT  // must be last
} grpc_chttp2_stream_list_id;

typedef enum {
  GRPC_CHTTP2_WRITE_STATE_IDLE,
  GRPC_CHTTP2_WRITE_STATE_WRITING,
  GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE,
} grpc_chttp2_write_state;

typedef enum {
  GRPC_CHTTP2_OPTIMIZE_FOR_LATENCY,
  GRPC_CHTTP2_OPTIMIZE_FOR_THROUGHPUT,
} grpc_chttp2_optimization_target;

typedef enum {
  GRPC_CHTTP2_PCL_INITIATE = 0,
  GRPC_CHTTP2_PCL_NEXT,
  GRPC_CHTTP2_PCL_INFLIGHT,
  GRPC_CHTTP2_PCL_COUNT  // must be last
} grpc_chttp2_ping_closure_list;

typedef enum {
  GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE,
  GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM,
  GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE,
  GRPC_CHTTP2_INITIATE_WRITE_SEND_INITIAL_METADATA,
  GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA,
  GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING,
  GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS,
  GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT,
  GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM,
  GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API,
  GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL,
  GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL,
  GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS,
  GRPC_CHTTP2_INITIATE_WRITE_SETTINGS_ACK,
  GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING,
  GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE,
  GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING,
  GRPC_CHTTP2_INITIATE_WRITE_BDP_PING,
  GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING,
  GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL_UNSTALLED,
  GRPC_CHTTP2_INITIATE_WRITE_PING_RESPONSE,
  GRPC_CHTTP2_INITIATE_WRITE_FORCE_RST_STREAM,
} grpc_chttp2_initiate_write_reason;

const char* grpc_chttp2_initiate_write_reason_string(
    grpc_chttp2_initiate_write_reason reason);

// deframer state for the overall http2 stream of bytes
typedef enum {
  // prefix: one entry per http2 connection prefix byte
  GRPC_DTS_CLIENT_PREFIX_0 = 0,
  GRPC_DTS_CLIENT_PREFIX_1,
  GRPC_DTS_CLIENT_PREFIX_2,
  GRPC_DTS_CLIENT_PREFIX_3,
  GRPC_DTS_CLIENT_PREFIX_4,
  GRPC_DTS_CLIENT_PREFIX_5,
  GRPC_DTS_CLIENT_PREFIX_6,
  GRPC_DTS_CLIENT_PREFIX_7,
  GRPC_DTS_CLIENT_PREFIX_8,
  GRPC_DTS_CLIENT_PREFIX_9,
  GRPC_DTS_CLIENT_PREFIX_10,
  GRPC_DTS_CLIENT_PREFIX_11,
  GRPC_DTS_CLIENT_PREFIX_12,
  GRPC_DTS_CLIENT_PREFIX_13,
  GRPC_DTS_CLIENT_PREFIX_14,
  GRPC_DTS_CLIENT_PREFIX_15,
  GRPC_DTS_CLIENT_PREFIX_16,
  GRPC_DTS_CLIENT_PREFIX_17,
  GRPC_DTS_CLIENT_PREFIX_18,
  GRPC_DTS_CLIENT_PREFIX_19,
  GRPC_DTS_CLIENT_PREFIX_20,
  GRPC_DTS_CLIENT_PREFIX_21,
  GRPC_DTS_CLIENT_PREFIX_22,
  GRPC_DTS_CLIENT_PREFIX_23,
  // frame header byte 0...
  // must follow from the prefix states
  GRPC_DTS_FH_0,
  GRPC_DTS_FH_1,
  GRPC_DTS_FH_2,
  GRPC_DTS_FH_3,
  GRPC_DTS_FH_4,
  GRPC_DTS_FH_5,
  GRPC_DTS_FH_6,
  GRPC_DTS_FH_7,
  // ... frame header byte 8
  GRPC_DTS_FH_8,
  // inside a http2 frame
  GRPC_DTS_FRAME
} grpc_chttp2_deframe_transport_state;

struct grpc_chttp2_stream_list {
  grpc_chttp2_stream* head;
  grpc_chttp2_stream* tail;
};
struct grpc_chttp2_stream_link {
  grpc_chttp2_stream* next;
  grpc_chttp2_stream* prev;
};

typedef enum {
  GRPC_CHTTP2_NO_GOAWAY_SEND,
  GRPC_CHTTP2_GRACEFUL_GOAWAY,
  GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED,
  GRPC_CHTTP2_FINAL_GOAWAY_SENT,
} grpc_chttp2_sent_goaway_state;

typedef struct grpc_chttp2_write_cb {
  int64_t call_at_byte;
  grpc_closure* closure;
  struct grpc_chttp2_write_cb* next;
} grpc_chttp2_write_cb;

typedef enum {
  GRPC_CHTTP2_KEEPALIVE_STATE_WAITING,
  GRPC_CHTTP2_KEEPALIVE_STATE_PINGING,
  GRPC_CHTTP2_KEEPALIVE_STATE_DYING,
  GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED,
} grpc_chttp2_keepalive_state;

typedef enum {
  GRPC_METADATA_NOT_PUBLISHED,
  GRPC_METADATA_SYNTHESIZED_FROM_FAKE,
  GRPC_METADATA_PUBLISHED_FROM_WIRE,
  GRPC_METADATA_PUBLISHED_AT_CLOSE
} grpc_published_metadata_method;

struct grpc_chttp2_stream {
  grpc_chttp2_stream(grpc_chttp2_transport* t, grpc_stream_refcount* refcount,
                     const void* server_data, grpc_core::Arena* arena);
  ~grpc_chttp2_stream();

  const grpc_core::RefCountedPtr<grpc_chttp2_transport> t;
  grpc_stream_refcount* refcount;
  grpc_core::Arena* const arena;

  grpc_closure destroy_stream;
  grpc_closure* destroy_stream_arg;

  grpc_chttp2_stream_link links[STREAM_LIST_COUNT];

  /// HTTP2 stream id for this stream, or zero if one has not been assigned
  uint32_t id = 0;

  /// things the upper layers would like to send
  grpc_metadata_batch* send_initial_metadata = nullptr;
  grpc_closure* send_initial_metadata_finished = nullptr;
  grpc_metadata_batch* send_trailing_metadata = nullptr;
  // TODO(yashykt): Find a better name for the below field and others in this
  //                struct to betteer distinguish inputs, return values, and
  //                internal state.
  // sent_trailing_metadata_op allows the transport to fill in to the upper
  // layer whether this stream was able to send its trailing metadata (used for
  // detecting cancellation on the server-side)..
  bool* sent_trailing_metadata_op = nullptr;
  grpc_closure* send_trailing_metadata_finished = nullptr;

  int64_t next_message_end_offset;
  int64_t flow_controlled_bytes_written = 0;
  int64_t flow_controlled_bytes_flowed = 0;
  grpc_closure* send_message_finished = nullptr;

  grpc_metadata_batch* recv_initial_metadata;
  grpc_closure* recv_initial_metadata_ready = nullptr;
  bool* trailing_metadata_available = nullptr;
  absl::optional<grpc_core::SliceBuffer>* recv_message = nullptr;
  uint32_t* recv_message_flags = nullptr;
  bool* call_failed_before_recv_message = nullptr;
  grpc_closure* recv_message_ready = nullptr;
  grpc_metadata_batch* recv_trailing_metadata;
  grpc_closure* recv_trailing_metadata_finished = nullptr;

  grpc_transport_stream_stats* collecting_stats = nullptr;
  grpc_transport_stream_stats stats = grpc_transport_stream_stats();

  /// Is this stream closed for writing.
  bool write_closed = false;
  /// Is this stream reading half-closed.
  bool read_closed = false;
  /// Are all published incoming byte streams closed.
  bool all_incoming_byte_streams_finished = false;
  /// Has this stream seen an error.
  /// If true, then pending incoming frames can be thrown away.
  bool seen_error = false;
  /// Are we buffering writes on this stream? If yes, we won't become writable
  /// until there's enough queued up in the flow_controlled_buffer
  bool write_buffering = false;

  // have we sent or received the EOS bit?
  bool eos_received = false;
  bool eos_sent = false;

  grpc_core::BitSet<STREAM_LIST_COUNT> included;

  /// the error that resulted in this stream being read-closed
  grpc_error_handle read_closed_error;
  /// the error that resulted in this stream being write-closed
  grpc_error_handle write_closed_error;

  grpc_published_metadata_method published_metadata[2] = {};

  grpc_metadata_batch initial_metadata_buffer;
  grpc_metadata_batch trailing_metadata_buffer;

  grpc_slice_buffer frame_storage;  // protected by t combiner

  grpc_core::Timestamp deadline = grpc_core::Timestamp::InfFuture();

  /// number of bytes received - reset at end of parse thread execution
  int64_t received_bytes = 0;

  grpc_core::chttp2::StreamFlowControl flow_control;

  grpc_slice_buffer flow_controlled_buffer;

  grpc_chttp2_write_cb* on_flow_controlled_cbs = nullptr;
  grpc_chttp2_write_cb* on_write_finished_cbs = nullptr;
  grpc_chttp2_write_cb* finish_after_write = nullptr;
  size_t sending_bytes = 0;

  /// Byte counter for number of bytes written
  size_t byte_counter = 0;

  /// Number of times written
  int64_t write_counter = 0;

  grpc_core::Chttp2CallTracerWrapper call_tracer_wrapper;

  /// Only set when enabled.
  // TODO(roth): Remove this when the call_tracer_in_transport
  // experiment finishes rolling out.
  grpc_core::CallTracerAnnotationInterface* call_tracer = nullptr;

  /// Only set when enabled.
  std::shared_ptr<grpc_core::TcpTracerInterface> tcp_tracer;

  // time this stream was created
  gpr_timespec creation_time = gpr_now(GPR_CLOCK_MONOTONIC);

  bool parsed_trailers_only = false;

  bool final_metadata_requested = false;
  bool received_last_frame = false;  // protected by t combiner

  /// how many header frames have we received?
  uint8_t header_frames_received = 0;

  bool sent_initial_metadata = false;
  bool sent_trailing_metadata = false;

  /// Whether the bytes needs to be traced using Fathom
  bool traced = false;
};

#define GRPC_ARG_PING_TIMEOUT_MS "grpc.http2.ping_timeout_ms"

// EXPERIMENTAL: provide protection against overloading a server with too many
// requests: wait for streams to be deallocated before they stop counting
// against MAX_CONCURRENT_STREAMS
#define GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION \
  "grpc.http.overload_protection"

/// Transport writing call flow:
/// grpc_chttp2_initiate_write() is called anywhere that we know bytes need to
/// go out on the wire.
/// If no other write has been started, a task is enqueued onto our workqueue.
/// When that task executes, it obtains the global lock, and gathers the data
/// to write.
/// The global lock is dropped and we do the syscall to write.
/// After writing, a follow-up check is made to see if another round of writing
/// should be performed.

/// The actual call chain is documented in the implementation of this function.
///
void grpc_chttp2_initiate_write(grpc_chttp2_transport* t,
                                grpc_chttp2_initiate_write_reason reason);

struct grpc_chttp2_begin_write_result {
  /// are we writing?
  bool writing;
  /// if writing: was it a complete flush (false) or a partial flush (true)
  bool partial;
  /// did we queue any completions as part of beginning the write
  bool early_results_scheduled;
};
grpc_chttp2_begin_write_result grpc_chttp2_begin_write(
    grpc_chttp2_transport* t);
void grpc_chttp2_end_write(grpc_chttp2_transport* t, grpc_error_handle error);

/// Process one slice of incoming data
/// Returns:
///  - a count of parsed bytes in the event of a partial read: the caller should
///    offload responsibilities to another thread to continue parsing.
///  - or a status in the case of a completed read
absl::variant<size_t, absl::Status> grpc_chttp2_perform_read(
    grpc_chttp2_transport* t, const grpc_slice& slice,
    size_t& requests_started);

bool grpc_chttp2_list_add_writable_stream(grpc_chttp2_transport* t,
                                          grpc_chttp2_stream* s);
/// Get a writable stream
/// returns non-zero if there was a stream available
bool grpc_chttp2_list_pop_writable_stream(grpc_chttp2_transport* t,
                                          grpc_chttp2_stream** s);
bool grpc_chttp2_list_remove_writable_stream(grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s);

bool grpc_chttp2_list_add_writing_stream(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream* s);
bool grpc_chttp2_list_have_writing_streams(grpc_chttp2_transport* t);
bool grpc_chttp2_list_pop_writing_stream(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream** s);

void grpc_chttp2_list_add_written_stream(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream* s);
bool grpc_chttp2_list_pop_written_stream(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream** s);

void grpc_chttp2_list_add_waiting_for_concurrency(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s);
bool grpc_chttp2_list_pop_waiting_for_concurrency(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream** s);
void grpc_chttp2_list_remove_waiting_for_concurrency(grpc_chttp2_transport* t,
                                                     grpc_chttp2_stream* s);

void grpc_chttp2_list_add_stalled_by_transport(grpc_chttp2_transport* t,
                                               grpc_chttp2_stream* s);
bool grpc_chttp2_list_pop_stalled_by_transport(grpc_chttp2_transport* t,
                                               grpc_chttp2_stream** s);
void grpc_chttp2_list_remove_stalled_by_transport(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s);

void grpc_chttp2_list_add_stalled_by_stream(grpc_chttp2_transport* t,
                                            grpc_chttp2_stream* s);
bool grpc_chttp2_list_pop_stalled_by_stream(grpc_chttp2_transport* t,
                                            grpc_chttp2_stream** s);
bool grpc_chttp2_list_remove_stalled_by_stream(grpc_chttp2_transport* t,
                                               grpc_chttp2_stream* s);

//******** Flow Control **************

// Takes in a flow control action and performs all the needed operations.
void grpc_chttp2_act_on_flowctl_action(
    const grpc_core::chttp2::FlowControlAction& action,
    grpc_chttp2_transport* t, grpc_chttp2_stream* s);

//******** End of Flow Control **************

inline grpc_chttp2_stream* grpc_chttp2_parsing_lookup_stream(
    grpc_chttp2_transport* t, uint32_t id) {
  auto it = t->stream_map.find(id);
  if (it == t->stream_map.end()) return nullptr;
  return it->second;
}
grpc_chttp2_stream* grpc_chttp2_parsing_accept_stream(grpc_chttp2_transport* t,
                                                      uint32_t id);

void grpc_chttp2_add_incoming_goaway(grpc_chttp2_transport* t,
                                     uint32_t goaway_error,
                                     uint32_t last_stream_id,
                                     absl::string_view goaway_text);

void grpc_chttp2_parsing_become_skip_parser(grpc_chttp2_transport* t);

void grpc_chttp2_complete_closure_step(grpc_chttp2_transport* t,
                                       grpc_closure** pclosure,
                                       grpc_error_handle error,
                                       const char* desc,
                                       grpc_core::DebugLocation whence = {});

void grpc_chttp2_keepalive_timeout(
    grpc_core::RefCountedPtr<grpc_chttp2_transport> t);
void grpc_chttp2_ping_timeout(
    grpc_core::RefCountedPtr<grpc_chttp2_transport> t);

void grpc_chttp2_settings_timeout(
    grpc_core::RefCountedPtr<grpc_chttp2_transport> t);

#define GRPC_HEADER_SIZE_IN_BYTES 5
#define MAX_SIZE_T (~(size_t)0)

#define GRPC_CHTTP2_CLIENT_CONNECT_STRING "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define GRPC_CHTTP2_CLIENT_CONNECT_STRLEN \
  (sizeof(GRPC_CHTTP2_CLIENT_CONNECT_STRING) - 1)

#define GRPC_CHTTP2_IF_TRACING(severity) \
  LOG_IF(severity, GRPC_TRACE_FLAG_ENABLED(http))

void grpc_chttp2_fake_status(grpc_chttp2_transport* t,
                             grpc_chttp2_stream* stream,
                             grpc_error_handle error);
void grpc_chttp2_start_writing(grpc_chttp2_transport* t);

#ifndef NDEBUG
#define GRPC_CHTTP2_STREAM_REF(stream, reason) \
  grpc_chttp2_stream_ref(stream, reason)
#define GRPC_CHTTP2_STREAM_UNREF(stream, reason) \
  grpc_chttp2_stream_unref(stream, reason)
void grpc_chttp2_stream_ref(grpc_chttp2_stream* s, const char* reason);
void grpc_chttp2_stream_unref(grpc_chttp2_stream* s, const char* reason);
#else
#define GRPC_CHTTP2_STREAM_REF(stream, reason) grpc_chttp2_stream_ref(stream)
#define GRPC_CHTTP2_STREAM_UNREF(stream, reason) \
  grpc_chttp2_stream_unref(stream)
void grpc_chttp2_stream_ref(grpc_chttp2_stream* s);
void grpc_chttp2_stream_unref(grpc_chttp2_stream* s);
#endif

void grpc_chttp2_ack_ping(grpc_chttp2_transport* t, uint64_t id);

/// Sends GOAWAY with error code ENHANCE_YOUR_CALM and additional debug data
/// resembling "too_many_pings" followed by immediately closing the connection.
void grpc_chttp2_exceeded_ping_strikes(grpc_chttp2_transport* t);

/// Resets ping clock. Should be called when flushing window updates,
/// initial/trailing metadata or data frames. For a server, it resets the number
/// of ping strikes and the last_ping_recv_time. For a ping sender, it resets
/// pings_before_data_required.
void grpc_chttp2_reset_ping_clock(grpc_chttp2_transport* t);

/// add a ref to the stream and add it to the writable list;
/// ref will be dropped in writing.c
void grpc_chttp2_mark_stream_writable(grpc_chttp2_transport* t,
                                      grpc_chttp2_stream* s);

void grpc_chttp2_cancel_stream(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                               grpc_error_handle due_to_error, bool tarpit);

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s);
void grpc_chttp2_maybe_complete_recv_message(grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s);
void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_chttp2_transport* t,
                                                       grpc_chttp2_stream* s);

void grpc_chttp2_fail_pending_writes(grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s,
                                     grpc_error_handle error);

/// Set the default keepalive configurations, must only be called at
/// initialization
void grpc_chttp2_config_default_keepalive_args(grpc_channel_args* args,
                                               bool is_client);
void grpc_chttp2_config_default_keepalive_args(
    const grpc_core::ChannelArgs& channel_args, bool is_client);

void grpc_chttp2_retry_initiate_ping(
    grpc_core::RefCountedPtr<grpc_chttp2_transport> t);

void schedule_bdp_ping_locked(
    grpc_core::RefCountedPtr<grpc_chttp2_transport> t);

uint32_t grpc_chttp2_min_read_progress_size(grpc_chttp2_transport* t);

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H
