// Copyright 2022 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/posix_engine/tcp_posix_endpoint.h"

#include <unordered_map>

#include "grpc/event_engine/slice_buffer.h"
#include <grpc/support/alloc.h>

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

#ifndef TCP_INQ
#define TCP_INQ 36
#define TCP_CM_INQ TCP_INQ
#endif

#ifdef GRPC_HAVE_MSG_NOSIGNAL
#define SENDMSG_FLAGS MSG_NOSIGNAL
#else
#define SENDMSG_FLAGS 0
#endif

// TCP zero copy sendmsg flag.
// NB: We define this here as a fallback in case we're using an older set of
// library headers that has not defined MSG_ZEROCOPY. Since this constant is
// part of the kernel, we are guaranteed it will never change/disagree so
// defining it here is safe.
#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

#ifdef GRPC_MSG_IOVLEN_TYPE
typedef GRPC_MSG_IOVLEN_TYPE msg_iovlen_type;
#else
typedef size_t msg_iovlen_type;
#endif

namespace grpc_event_engine {
namespace posix_engine {

using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::SliceBuffer;

class TcpZerocopySendRecord {
 public:
  TcpZerocopySendRecord() : buf_(){};

  ~TcpZerocopySendRecord() { AssertEmpty(); }

  // Given the slices that we wish to send, and the current offset into the
  //   slice buffer (indicating which have already been sent), populate an iovec
  //   array that will be used for a zerocopy enabled sendmsg().
  msg_iovlen_type PopulateIovs(size_t* unwind_slice_idx,
                               size_t* unwind_byte_idx, size_t* sending_length,
                               iovec* iov);

  // A sendmsg() may not be able to send the bytes that we requested at this
  // time, returning EAGAIN (possibly due to backpressure). In this case,
  // unwind the offset into the slice buffer so we retry sending these bytes.
  void UnwindIfThrottled(size_t unwind_slice_idx, size_t unwind_byte_idx) {
    out_offset_.byte_idx = unwind_byte_idx;
    out_offset_.slice_idx = unwind_slice_idx;
  }

  // Update the offset into the slice buffer based on how much we wanted to sent
  // vs. what sendmsg() actually sent (which may be lower, possibly due to
  // backpressure).
  void UpdateOffsetForBytesSent(size_t sending_length, size_t actually_sent);

  // Indicates whether all underlying data has been sent or not.
  bool AllSlicesSent() { return out_offset_.slice_idx == buf_.Count(); }

  // Reset this structure for a new tcp_write() with zerocopy.
  void PrepareForSends(SliceBuffer& slices_to_send) {
    AssertEmpty();
    out_offset_.slice_idx = 0;
    out_offset_.byte_idx = 0;
    buf_.Swap(slices_to_send);
    Ref();
  }

  // References: 1 reference per sendmsg(), and 1 for the tcp_write().
  void Ref() { ref_.fetch_add(1, std::memory_order_relaxed); }

  // Unref: called when we get an error queue notification for a sendmsg(), if a
  //  sendmsg() failed or when tcp_write() is done.
  bool Unref() {
    const intptr_t prior = ref_.fetch_sub(1, std::memory_order_acq_rel);
    GPR_DEBUG_ASSERT(prior > 0);
    if (prior == 1) {
      AllSendsComplete();
      return true;
    }
    return false;
  }

 private:
  struct OutgoingOffset {
    size_t slice_idx = 0;
    size_t byte_idx = 0;
  };

  void AssertEmpty() {
    GPR_DEBUG_ASSERT(buf_.Count() == 0);
    GPR_DEBUG_ASSERT(buf_.Length() == 0);
    GPR_DEBUG_ASSERT(ref_.load(std::memory_order_relaxed) == 0);
  }

  // When all sendmsg() calls associated with this tcp_write() have been
  // completed (ie. we have received the notifications for each sequence number
  // for each sendmsg()) and all reference counts have been dropped, drop our
  // reference to the underlying data since we no longer need it.
  void AllSendsComplete() {
    GPR_DEBUG_ASSERT(ref_.load(std::memory_order_relaxed) == 0);
    buf_.Clear();
  }

  SliceBuffer buf_;
  std::atomic<intptr_t> ref_{0};
  OutgoingOffset out_offset_;
};

class TcpZerocopySendCtx {
 public:
  static constexpr int kDefaultMaxSends = 4;
  static constexpr size_t kDefaultSendBytesThreshold = 16 * 1024;  // 16KB

  explicit TcpZerocopySendCtx(
      int max_sends = kDefaultMaxSends,
      size_t send_bytes_threshold = kDefaultSendBytesThreshold)
      : max_sends_(max_sends),
        free_send_records_size_(max_sends),
        threshold_bytes_(send_bytes_threshold) {
    send_records_ = static_cast<TcpZerocopySendRecord*>(
        gpr_malloc(max_sends * sizeof(*send_records_)));
    free_send_records_ = static_cast<TcpZerocopySendRecord**>(
        gpr_malloc(max_sends * sizeof(*free_send_records_)));
    if (send_records_ == nullptr || free_send_records_ == nullptr) {
      gpr_free(send_records_);
      gpr_free(free_send_records_);
      gpr_log(GPR_INFO, "Disabling TCP TX zerocopy due to memory pressure.\n");
      memory_limited_ = true;
    } else {
      for (int idx = 0; idx < max_sends_; ++idx) {
        new (send_records_ + idx) TcpZerocopySendRecord();
        free_send_records_[idx] = send_records_ + idx;
      }
    }
  }

  ~TcpZerocopySendCtx() {
    if (send_records_ != nullptr) {
      for (int idx = 0; idx < max_sends_; ++idx) {
        send_records_[idx].~TcpZerocopySendRecord();
      }
    }
    gpr_free(send_records_);
    gpr_free(free_send_records_);
  }

  // True if we were unable to allocate the various bookkeeping structures at
  // transport initialization time. If memory limited, we do not zerocopy.
  bool memory_limited() const { return memory_limited_; }

  // TCP send zerocopy maintains an implicit sequence number for every
  // successful sendmsg() with zerocopy enabled; the kernel later gives us an
  // error queue notification with this sequence number indicating that the
  // underlying data buffers that we sent can now be released. Once that
  // notification is received, we can release the buffers associated with this
  // zerocopy send record. Here, we associate the sequence number with the data
  // buffers that were sent with the corresponding call to sendmsg().
  void NoteSend(TcpZerocopySendRecord* record) {
    record->Ref();
    {
      absl::MutexLock guard(&lock_);
      is_in_write_ = true;
      AssociateSeqWithSendRecordLocked(last_send_, record);
    }
    ++last_send_;
  }

  // If sendmsg() actually failed, though, we need to revert the sequence number
  // that we speculatively bumped before calling sendmsg(). Note that we bump
  // this sequence number and perform relevant bookkeeping (see: NoteSend())
  // *before* calling sendmsg() since, if we called it *after* sendmsg(), then
  // there is a possible race with the release notification which could occur on
  // another thread before we do the necessary bookkeeping. Hence, calling
  // NoteSend() *before* sendmsg() and implementing an undo function is needed.
  void UndoSend() {
    --last_send_;
    if (ReleaseSendRecord(last_send_)->Unref()) {
      // We should still be holding the ref taken by tcp_write().
      GPR_DEBUG_ASSERT(0);
    }
  }

  // Simply associate this send record (and the underlying sent data buffers)
  // with the implicit sequence number for this zerocopy sendmsg().
  void AssociateSeqWithSendRecordLocked(uint32_t seq,
                                        TcpZerocopySendRecord* record) {
    ctx_lookup_.emplace(seq, record);
  }

  // Get a send record for a send that we wish to do with zerocopy.
  TcpZerocopySendRecord* GetSendRecord() {
    absl::MutexLock guard(&lock_);
    return TryGetSendRecordLocked();
  }

  // A given send record corresponds to a single tcp_write() with zerocopy
  // enabled. This can result in several sendmsg() calls to flush all of the
  // data to wire. Each sendmsg() takes a reference on the
  // TcpZerocopySendRecord, and corresponds to a single sequence number.
  // ReleaseSendRecord releases a reference on TcpZerocopySendRecord for a
  // single sequence number. This is called either when we receive the relevant
  // error queue notification (saying that we can discard the underlying
  // buffers for this sendmsg()) is received from the kernel - or, in case
  // sendmsg() was unsuccessful to begin with.
  TcpZerocopySendRecord* ReleaseSendRecord(uint32_t seq) {
    absl::MutexLock guard(&lock_);
    return ReleaseSendRecordLocked(seq);
  }

  // After all the references to a TcpZerocopySendRecord are released, we can
  // add it back to the pool (of size max_sends_). Note that we can only have
  // max_sends_ tcp_write() instances with zerocopy enabled in flight at the
  // same time.
  void PutSendRecord(TcpZerocopySendRecord* record) {
    GPR_DEBUG_ASSERT(record >= send_records_ &&
                     record < send_records_ + max_sends_);
    absl::MutexLock guard(&lock_);
    PutSendRecordLocked(record);
  }

  // Indicate that we are disposing of this zerocopy context. This indicator
  // will prevent new zerocopy writes from being issued.
  void Shutdown() { shutdown_.store(true, std::memory_order_release); }

  // Indicates that there are no inflight tcp_write() instances with zerocopy
  // enabled.
  bool AllSendRecordsEmpty() {
    absl::MutexLock guard(&lock_);
    return free_send_records_size_ == max_sends_;
  }

  bool enabled() const { return enabled_; }

  void set_enabled(bool enabled) {
    GPR_DEBUG_ASSERT(!enabled || !memory_limited());
    enabled_ = enabled;
  }

  // Only use zerocopy if we are sending at least this many bytes. The
  // additional overhead of reading the error queue for notifications means that
  // zerocopy is not useful for small transfers.
  size_t threshold_bytes() const { return threshold_bytes_; }

  // Expected to be called by handler reading messages from the err queue.
  // It is used to indicate that some OMem meory is now available. It returns
  // true to tell the caller to mark the file descriptor as immediately
  // writable.
  //
  // If a write is currently in progress on the socket (ie. we have issued a
  // sendmsg() and are about to check its return value) then we set omem state
  // to CHECK to make the sending thread know that some tcp_omem was
  // concurrently freed even if sendmsg() returns ENOBUFS. In this case, since
  // there is already an active send thread, we do not need to mark the
  // socket writeable, so we return false.
  //
  // If there was no write in progress on the socket, and the socket was not
  // marked as FULL, then we need not mark the socket writeable now that some
  // tcp_omem memory is freed since it was not considered as blocked on
  // tcp_omem to begin with. So in this case, return false.
  //
  // But, if a write was not in progress and the omem state was FULL, then we
  // need to mark the socket writeable since it is no longer blocked by
  // tcp_omem. In this case, return true.
  //
  // Please refer to the STATE TRANSITION DIAGRAM below for more details.
  //
  bool UpdateZeroCopyOMemStateAfterFree() {
    absl::MutexLock guard(&lock_);
    if (is_in_write_) {
      zcopy_enobuf_state_ = OMemState::CHECK;
      return false;
    }
    GPR_DEBUG_ASSERT(zcopy_enobuf_state_ != OMemState::CHECK);
    if (zcopy_enobuf_state_ == OMemState::FULL) {
      // A previous sendmsg attempt was blocked by ENOBUFS. Return true to
      // mark the fd as writable so the next write attempt could be made.
      zcopy_enobuf_state_ = OMemState::OPEN;
      return true;
    } else if (zcopy_enobuf_state_ == OMemState::OPEN) {
      // No need to mark the fd as writable because the previous write
      // attempt did not encounter ENOBUFS.
      return false;
    } else {
      // This state should never be reached because it implies that the previous
      // state was CHECK and is_in_write is false. This means that after the
      // previous sendmsg returned and set is_in_write to false, it did
      // not update the z-copy change from CHECK to OPEN.
      GPR_ASSERT(false && "OMem state error!");
    }
  }

  // Expected to be called by the thread calling sendmsg after the syscall
  // invocation. is complete. If an ENOBUF is seen, it checks if the error
  // handler (Tx0cp completions) has already run and free'ed up some OMem. It
  // returns true indicating that the write can be attempted again immediately.
  // If ENOBUFS was seen but no Tx0cp completions have been received between the
  // sendmsg() and us taking this lock, then tcp_omem is still full from our
  // point of view. Therefore, we do not signal that the socket is writeable
  // with respect to the availability of tcp_omem. Therefore the function
  // returns false. This indicates that another write should not be attempted
  // immediately and the calling thread should wait until the socket is writable
  // again. If ENOBUFS was not seen, then again return false because the next
  // write should be attempted only when the socket is writable again.
  //
  // Please refer to the STATE TRANSITION DIAGRAM below for more details.
  //
  bool UpdateZeroCopyOMemStateAfterSend(bool seen_enobuf) {
    absl::MutexLock guard(&lock_);
    is_in_write_ = false;
    if (seen_enobuf) {
      if (zcopy_enobuf_state_ == OMemState::CHECK) {
        zcopy_enobuf_state_ = OMemState::OPEN;
        return true;
      } else {
        zcopy_enobuf_state_ = OMemState::FULL;
      }
    } else if (zcopy_enobuf_state_ != OMemState::OPEN) {
      zcopy_enobuf_state_ = OMemState::OPEN;
    }
    return false;
  }

 private:
  //                      STATE TRANSITION DIAGRAM
  //
  // sendmsg succeeds       Tx-zero copy succeeds and there is no active sendmsg
  //      ----<<--+  +------<<-------------------------------------+
  //      |       |  |                                             |
  //      |       |  v       sendmsg returns ENOBUFS               |
  //      +-----> OPEN  ------------->>-------------------------> FULL
  //                ^                                              |
  //                |                                              |
  //                | sendmsg completes                            |
  //                +----<<---------- CHECK <-------<<-------------+
  //                                        Tx-zero copy succeeds and there is
  //                                        an active sendmsg
  //
  enum class OMemState : int8_t {
    OPEN,   // Everything is clear and omem is not full.
    FULL,   // The last sendmsg() has returned with an errno of ENOBUFS.
    CHECK,  // Error queue is read while is_in_write_ was true, so we should
            // check this state after the sendmsg.
  };

  TcpZerocopySendRecord* ReleaseSendRecordLocked(uint32_t seq) {
    auto iter = ctx_lookup_.find(seq);
    GPR_DEBUG_ASSERT(iter != ctx_lookup_.end());
    TcpZerocopySendRecord* record = iter->second;
    ctx_lookup_.erase(iter);
    return record;
  }

  TcpZerocopySendRecord* TryGetSendRecordLocked() {
    if (shutdown_.load(std::memory_order_acquire)) {
      return nullptr;
    }
    if (free_send_records_size_ == 0) {
      return nullptr;
    }
    free_send_records_size_--;
    return free_send_records_[free_send_records_size_];
  }

  void PutSendRecordLocked(TcpZerocopySendRecord* record) {
    GPR_DEBUG_ASSERT(free_send_records_size_ < max_sends_);
    free_send_records_[free_send_records_size_] = record;
    free_send_records_size_++;
  }

  TcpZerocopySendRecord* send_records_;
  TcpZerocopySendRecord** free_send_records_;
  int max_sends_;
  int free_send_records_size_;
  absl::Mutex lock_;
  uint32_t last_send_ = 0;
  std::atomic<bool> shutdown_{false};
  bool enabled_ = false;
  size_t threshold_bytes_ = kDefaultSendBytesThreshold;
  std::unordered_map<uint32_t, TcpZerocopySendRecord*> ctx_lookup_;
  bool memory_limited_ = false;
  bool is_in_write_ = false;
  OMemState zcopy_enobuf_state_;
};

PosixEndpoint::PosixEndpoint(EventHandle* handle,
                             const PosixTcpOptions& options)
    : handle_(handle), poller_(handle->Poller()), options_(options) {
  fd_ = handle_->WrappedFd();
  GPR_ASSERT(options.resource_quota != nullptr);
  memory_owner_ =
      options.resource_quota->memory_quota()->CreateMemoryOwner(peer_string);
  tcp->self_reservation = tcp->memory_owner.MakeReservation(sizeof(grpc_tcp));
  grpc_resolved_address resolved_local_addr;
  memset(&resolved_local_addr, 0, sizeof(resolved_local_addr));
  resolved_local_addr.len = sizeof(resolved_local_addr.addr);
  absl::StatusOr<std::string> addr_uri;
  if (getsockname(tcp->fd,
                  reinterpret_cast<sockaddr*>(resolved_local_addr.addr),
                  &resolved_local_addr.len) < 0 ||
      !(addr_uri = grpc_sockaddr_to_uri(&resolved_local_addr)).ok()) {
    tcp->local_address = "";
  } else {
    tcp->local_address = addr_uri.value();
  }
  tcp->read_cb = nullptr;
  tcp->write_cb = nullptr;
  tcp->current_zerocopy_send = nullptr;
  tcp->release_fd_cb = nullptr;
  tcp->release_fd = nullptr;
  tcp->target_length = static_cast<double>(options.tcp_read_chunk_size);
  tcp->bytes_read_this_round = 0;
  /* Will be set to false by the very first endpoint read function */
  tcp->is_first_read = true;
  tcp->has_posted_reclaimer = false;
  tcp->bytes_counter = -1;
  tcp->socket_ts_enabled = false;
  tcp->ts_capable = true;
  tcp->outgoing_buffer_arg = nullptr;
  tcp->frame_size_tuning_enabled = ExperimentalTcpFrameSizeTuningEnabled();
  tcp->min_progress_size = 1;
  if (options.tcp_tx_zero_copy_enabled &&
      !tcp->tcp_zerocopy_send_ctx.memory_limited()) {
#ifdef GRPC_LINUX_ERRQUEUE
    const int enable = 1;
    auto err =
        setsockopt(tcp->fd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
    if (err == 0) {
      tcp->tcp_zerocopy_send_ctx.set_enabled(true);
    } else {
      gpr_log(GPR_ERROR, "Failed to set zerocopy options on the socket.");
    }
#endif
  }
  /* paired with unref in grpc_tcp_destroy */
  new (&tcp->refcount) grpc_core::RefCount(
      1, GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace) ? "tcp" : nullptr);
  gpr_atm_no_barrier_store(&tcp->shutdown_count, 0);
  tcp->em_fd = em_fd;
  grpc_slice_buffer_init(&tcp->last_read_buffer);
  gpr_mu_init(&tcp->tb_mu);
  tcp->tb_head = nullptr;
  GRPC_CLOSURE_INIT(&tcp->read_done_closure, tcp_handle_read, tcp,
                    grpc_schedule_on_exec_ctx);
  if (grpc_event_engine_run_in_background()) {
    // If there is a polling engine always running in the background, there is
    // no need to run the backup poller.
    GRPC_CLOSURE_INIT(&tcp->write_done_closure, tcp_handle_write, tcp,
                      grpc_schedule_on_exec_ctx);
  } else {
    GRPC_CLOSURE_INIT(&tcp->write_done_closure,
                      tcp_drop_uncovered_then_handle_write, tcp,
                      grpc_schedule_on_exec_ctx);
  }
  /* Always assume there is something on the queue to read. */
  tcp->inq = 1;
#ifdef GRPC_HAVE_TCP_INQ
  int one = 1;
  if (setsockopt(tcp->fd, SOL_TCP, TCP_INQ, &one, sizeof(one)) == 0) {
    tcp->inq_capable = true;
  } else {
    gpr_log(GPR_DEBUG, "cannot set inq fd=%d errno=%d", tcp->fd, errno);
    tcp->inq_capable = false;
  }
#else
  tcp->inq_capable = false;
#endif /* GRPC_HAVE_TCP_INQ */
  /* Start being notified on errors if event engine can track errors. */
  if (grpc_event_engine_can_track_errors()) {
    /* Grab a ref to tcp so that we can safely access the tcp struct when
     * processing errors. We unref when we no longer want to track errors
     * separately. */
    TCP_REF(tcp, "error-tracking");
    gpr_atm_rel_store(&tcp->stop_error_notification, 0);
    GRPC_CLOSURE_INIT(&tcp->error_closure, tcp_handle_error, tcp,
                      grpc_schedule_on_exec_ctx);
    grpc_fd_notify_on_error(tcp->em_fd, &tcp->error_closure);
  }
}

}  // namespace posix_engine
}  // namespace grpc_event_engine