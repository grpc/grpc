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

#include <grpc/support/port_platform.h>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/grpc_types.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <unordered_map>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/event_log.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/trace.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

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

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_core {

class TcpZerocopySendRecord {
 public:
  TcpZerocopySendRecord() { grpc_slice_buffer_init(&buf_); }

  ~TcpZerocopySendRecord() {
    AssertEmpty();
    grpc_slice_buffer_destroy(&buf_);
  }

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
  bool AllSlicesSent() { return out_offset_.slice_idx == buf_.count; }

  // Reset this structure for a new tcp_write() with zerocopy.
  void PrepareForSends(grpc_slice_buffer* slices_to_send) {
    AssertEmpty();
    out_offset_.slice_idx = 0;
    out_offset_.byte_idx = 0;
    grpc_slice_buffer_swap(slices_to_send, &buf_);
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
    GPR_DEBUG_ASSERT(buf_.count == 0);
    GPR_DEBUG_ASSERT(buf_.length == 0);
    GPR_DEBUG_ASSERT(ref_.load(std::memory_order_relaxed) == 0);
  }

  // When all sendmsg() calls associated with this tcp_write() have been
  // completed (ie. we have received the notifications for each sequence number
  // for each sendmsg()) and all reference counts have been dropped, drop our
  // reference to the underlying data since we no longer need it.
  void AllSendsComplete() {
    GPR_DEBUG_ASSERT(ref_.load(std::memory_order_relaxed) == 0);
    grpc_slice_buffer_reset_and_unref(&buf_);
  }

  grpc_slice_buffer buf_;
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
      MutexLock guard(&lock_);
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
    MutexLock guard(&lock_);
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
    MutexLock guard(&lock_);
    return ReleaseSendRecordLocked(seq);
  }

  // After all the references to a TcpZerocopySendRecord are released, we can
  // add it back to the pool (of size max_sends_). Note that we can only have
  // max_sends_ tcp_write() instances with zerocopy enabled in flight at the
  // same time.
  void PutSendRecord(TcpZerocopySendRecord* record) {
    GPR_DEBUG_ASSERT(record >= send_records_ &&
                     record < send_records_ + max_sends_);
    MutexLock guard(&lock_);
    PutSendRecordLocked(record);
  }

  // Indicate that we are disposing of this zerocopy context. This indicator
  // will prevent new zerocopy writes from being issued.
  void Shutdown() { shutdown_.store(true, std::memory_order_release); }

  // Indicates that there are no inflight tcp_write() instances with zerocopy
  // enabled.
  bool AllSendRecordsEmpty() {
    MutexLock guard(&lock_);
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
    MutexLock guard(&lock_);
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
      Crash("OMem state error!");
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
    MutexLock guard(&lock_);
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
  Mutex lock_;
  uint32_t last_send_ = 0;
  std::atomic<bool> shutdown_{false};
  bool enabled_ = false;
  size_t threshold_bytes_ = kDefaultSendBytesThreshold;
  std::unordered_map<uint32_t, TcpZerocopySendRecord*> ctx_lookup_;
  bool memory_limited_ = false;
  bool is_in_write_ = false;
  OMemState zcopy_enobuf_state_ = OMemState::OPEN;
};

}  // namespace grpc_core

using grpc_core::TcpZerocopySendCtx;
using grpc_core::TcpZerocopySendRecord;

namespace {

struct grpc_tcp {
  explicit grpc_tcp(const grpc_core::PosixTcpOptions& tcp_options)
      : min_read_chunk_size(tcp_options.tcp_min_read_chunk_size),
        max_read_chunk_size(tcp_options.tcp_max_read_chunk_size),
        tcp_zerocopy_send_ctx(
            tcp_options.tcp_tx_zerocopy_max_simultaneous_sends,
            tcp_options.tcp_tx_zerocopy_send_bytes_threshold) {}
  grpc_endpoint base;
  grpc_fd* em_fd;
  int fd;
  int inq;  // bytes pending on the socket from the last read.
  double target_length;
  double bytes_read_this_round;
  grpc_core::RefCount refcount;
  gpr_atm shutdown_count;

  int min_read_chunk_size;
  int max_read_chunk_size;

  // garbage after the last read
  grpc_slice_buffer last_read_buffer;

  grpc_core::Mutex read_mu;
  grpc_slice_buffer* incoming_buffer ABSL_GUARDED_BY(read_mu) = nullptr;

  grpc_slice_buffer* outgoing_buffer;
  // byte within outgoing_buffer->slices[0] to write next
  size_t outgoing_byte_idx;

  grpc_closure* read_cb;
  grpc_closure* write_cb;
  grpc_closure* release_fd_cb;
  int* release_fd;

  grpc_closure read_done_closure;
  grpc_closure write_done_closure;
  grpc_closure error_closure;

  std::string peer_string;
  std::string local_address;

  grpc_core::MemoryOwner memory_owner;
  grpc_core::MemoryAllocator::Reservation self_reservation;

  grpc_core::TracedBufferList tb_list;  // List of traced buffers

  // grpc_endpoint_write takes an argument which if non-null means that the
  // transport layer wants the TCP layer to collect timestamps for this write.
  // This arg is forwarded to the timestamps callback function when the ACK
  // timestamp is received from the kernel. This arg is a (void *) which allows
  // users of this API to pass in a pointer to any kind of structure. This
  // structure could actually be a tag or any book-keeping object that the user
  // can use to distinguish between different traced writes. The only
  // requirement from the TCP endpoint layer is that this arg should be non-null
  // if the user wants timestamps for the write.
  void* outgoing_buffer_arg;
  // A counter which starts at 0. It is initialized the first time the socket
  // options for collecting timestamps are set, and is incremented with each
  // byte sent.
  int bytes_counter;

  int min_progress_size;  // A hint from upper layers specifying the minimum
                          // number of bytes that need to be read to make
                          // meaningful progress

  gpr_atm stop_error_notification;  // Set to 1 if we do not want to be notified
                                    // on errors anymore
  TcpZerocopySendCtx tcp_zerocopy_send_ctx;
  TcpZerocopySendRecord* current_zerocopy_send = nullptr;

  int set_rcvlowat = 0;

  // Used by the endpoint read function to distinguish the very first read call
  // from the rest
  bool is_first_read;
  bool has_posted_reclaimer ABSL_GUARDED_BY(read_mu) = false;
  bool inq_capable;        // cache whether kernel supports inq
  bool socket_ts_enabled;  // True if timestamping options are set on the socket
                           //
  bool ts_capable;         // Cache whether we can set timestamping options
};

struct backup_poller {
  gpr_mu* pollset_mu;
  grpc_closure run_poller;
};

}  // namespace

static void ZerocopyDisableAndWaitForRemaining(grpc_tcp* tcp);

#define BACKUP_POLLER_POLLSET(b) ((grpc_pollset*)((b) + 1))

static grpc_core::Mutex* g_backup_poller_mu = nullptr;
static int g_uncovered_notifications_pending
    ABSL_GUARDED_BY(g_backup_poller_mu);
static backup_poller* g_backup_poller ABSL_GUARDED_BY(g_backup_poller_mu);

static void tcp_handle_read(void* arg /* grpc_tcp */, grpc_error_handle error);
static void tcp_handle_write(void* arg /* grpc_tcp */, grpc_error_handle error);
static void tcp_drop_uncovered_then_handle_write(void* arg /* grpc_tcp */,
                                                 grpc_error_handle error);

static void done_poller(void* bp, grpc_error_handle /*error_ignored*/) {
  backup_poller* p = static_cast<backup_poller*>(bp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p destroy", p);
  }
  grpc_pollset_destroy(BACKUP_POLLER_POLLSET(p));
  gpr_free(p);
}

static void run_poller(void* bp, grpc_error_handle /*error_ignored*/) {
  backup_poller* p = static_cast<backup_poller*>(bp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p run", p);
  }
  gpr_mu_lock(p->pollset_mu);
  grpc_core::Timestamp deadline =
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(10);
  GRPC_LOG_IF_ERROR(
      "backup_poller:pollset_work",
      grpc_pollset_work(BACKUP_POLLER_POLLSET(p), nullptr, deadline));
  gpr_mu_unlock(p->pollset_mu);
  g_backup_poller_mu->Lock();
  // last "uncovered" notification is the ref that keeps us polling
  if (g_uncovered_notifications_pending == 1) {
    GPR_ASSERT(g_backup_poller == p);
    g_backup_poller = nullptr;
    g_uncovered_notifications_pending = 0;
    g_backup_poller_mu->Unlock();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p shutdown", p);
    }
    grpc_pollset_shutdown(BACKUP_POLLER_POLLSET(p),
                          GRPC_CLOSURE_INIT(&p->run_poller, done_poller, p,
                                            grpc_schedule_on_exec_ctx));
  } else {
    g_backup_poller_mu->Unlock();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p reschedule", p);
    }
    grpc_core::Executor::Run(&p->run_poller, absl::OkStatus(),
                             grpc_core::ExecutorType::DEFAULT,
                             grpc_core::ExecutorJobType::LONG);
  }
}

static void drop_uncovered(grpc_tcp* /*tcp*/) {
  int old_count;
  backup_poller* p;
  g_backup_poller_mu->Lock();
  p = g_backup_poller;
  old_count = g_uncovered_notifications_pending--;
  g_backup_poller_mu->Unlock();
  GPR_ASSERT(old_count > 1);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p uncover cnt %d->%d", p, old_count,
            old_count - 1);
  }
}

// gRPC API considers a Write operation to be done the moment it clears ‘flow
// control’ i.e., not necessarily sent on the wire. This means that the
// application MIGHT not call `grpc_completion_queue_next/pluck` in a timely
// manner when its `Write()` API is acked.
//
// We need to ensure that the fd is 'covered' (i.e being monitored by some
// polling thread and progress is made) and hence add it to a backup poller here
static void cover_self(grpc_tcp* tcp) {
  backup_poller* p;
  g_backup_poller_mu->Lock();
  int old_count = 0;
  if (g_uncovered_notifications_pending == 0) {
    g_uncovered_notifications_pending = 2;
    p = static_cast<backup_poller*>(
        gpr_zalloc(sizeof(*p) + grpc_pollset_size()));
    g_backup_poller = p;
    grpc_pollset_init(BACKUP_POLLER_POLLSET(p), &p->pollset_mu);
    g_backup_poller_mu->Unlock();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p create", p);
    }
    grpc_core::Executor::Run(
        GRPC_CLOSURE_INIT(&p->run_poller, run_poller, p, nullptr),
        absl::OkStatus(), grpc_core::ExecutorType::DEFAULT,
        grpc_core::ExecutorJobType::LONG);
  } else {
    old_count = g_uncovered_notifications_pending++;
    p = g_backup_poller;
    g_backup_poller_mu->Unlock();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p add %p cnt %d->%d", p, tcp,
            old_count - 1, old_count);
  }
  grpc_pollset_add_fd(BACKUP_POLLER_POLLSET(p), tcp->em_fd);
}

static void notify_on_read(grpc_tcp* tcp) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p notify_on_read", tcp);
  }
  grpc_fd_notify_on_read(tcp->em_fd, &tcp->read_done_closure);
}

static void notify_on_write(grpc_tcp* tcp) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p notify_on_write", tcp);
  }
  if (!grpc_event_engine_run_in_background()) {
    cover_self(tcp);
  }
  grpc_fd_notify_on_write(tcp->em_fd, &tcp->write_done_closure);
}

static void tcp_drop_uncovered_then_handle_write(void* arg,
                                                 grpc_error_handle error) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p got_write: %s", arg,
            grpc_core::StatusToString(error).c_str());
  }
  drop_uncovered(static_cast<grpc_tcp*>(arg));
  tcp_handle_write(arg, error);
}

static void add_to_estimate(grpc_tcp* tcp, size_t bytes) {
  tcp->bytes_read_this_round += static_cast<double>(bytes);
}

static void finish_estimate(grpc_tcp* tcp) {
  // If we read >80% of the target buffer in one read loop, increase the size
  // of the target buffer to either the amount read, or twice its previous
  // value
  if (tcp->bytes_read_this_round > tcp->target_length * 0.8) {
    tcp->target_length =
        std::max(2 * tcp->target_length, tcp->bytes_read_this_round);
  } else {
    tcp->target_length =
        0.99 * tcp->target_length + 0.01 * tcp->bytes_read_this_round;
  }
  tcp->bytes_read_this_round = 0;
}

static grpc_error_handle tcp_annotate_error(grpc_error_handle src_error,
                                            grpc_tcp* tcp) {
  return grpc_error_set_str(
      grpc_error_set_int(
          grpc_error_set_int(src_error, grpc_core::StatusIntProperty::kFd,
                             tcp->fd),
          // All tcp errors are marked with UNAVAILABLE so that application may
          // choose to retry.
          grpc_core::StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE),
      grpc_core::StatusStrProperty::kTargetAddress, tcp->peer_string);
}

static void tcp_handle_read(void* arg /* grpc_tcp */, grpc_error_handle error);
static void tcp_handle_write(void* arg /* grpc_tcp */, grpc_error_handle error);

static void tcp_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  ZerocopyDisableAndWaitForRemaining(tcp);
  grpc_fd_shutdown(tcp->em_fd, why);
  tcp->read_mu.Lock();
  tcp->memory_owner.Reset();
  tcp->read_mu.Unlock();
}

static void tcp_free(grpc_tcp* tcp) {
  grpc_fd_orphan(tcp->em_fd, tcp->release_fd_cb, tcp->release_fd,
                 "tcp_unref_orphan");
  grpc_slice_buffer_destroy(&tcp->last_read_buffer);
  tcp->tb_list.Shutdown(tcp->outgoing_buffer_arg,
                        GRPC_ERROR_CREATE("endpoint destroyed"));
  tcp->outgoing_buffer_arg = nullptr;
  delete tcp;
}

#ifndef NDEBUG
#define TCP_UNREF(tcp, reason) tcp_unref((tcp), (reason), DEBUG_LOCATION)
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), DEBUG_LOCATION)
static void tcp_unref(grpc_tcp* tcp, const char* reason,
                      const grpc_core::DebugLocation& debug_location) {
  if (GPR_UNLIKELY(tcp->refcount.Unref(debug_location, reason))) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp* tcp, const char* reason,
                    const grpc_core::DebugLocation& debug_location) {
  tcp->refcount.Ref(debug_location, reason);
}
#else
#define TCP_UNREF(tcp, reason) tcp_unref((tcp))
#define TCP_REF(tcp, reason) tcp_ref((tcp))
static void tcp_unref(grpc_tcp* tcp) {
  if (GPR_UNLIKELY(tcp->refcount.Unref())) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp* tcp) { tcp->refcount.Ref(); }
#endif

static void tcp_destroy(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_slice_buffer_reset_and_unref(&tcp->last_read_buffer);
  if (grpc_event_engine_can_track_errors()) {
    ZerocopyDisableAndWaitForRemaining(tcp);
    gpr_atm_no_barrier_store(&tcp->stop_error_notification, true);
    grpc_fd_set_error(tcp->em_fd);
  }
  tcp->read_mu.Lock();
  tcp->memory_owner.Reset();
  tcp->read_mu.Unlock();
  TCP_UNREF(tcp, "destroy");
}

static void perform_reclamation(grpc_tcp* tcp)
    ABSL_LOCKS_EXCLUDED(tcp->read_mu) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "TCP: benign reclamation to free memory");
  }
  tcp->read_mu.Lock();
  if (tcp->incoming_buffer != nullptr) {
    grpc_slice_buffer_reset_and_unref(tcp->incoming_buffer);
  }
  tcp->has_posted_reclaimer = false;
  tcp->read_mu.Unlock();
}

static void maybe_post_reclaimer(grpc_tcp* tcp)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(tcp->read_mu) {
  if (!tcp->has_posted_reclaimer) {
    tcp->has_posted_reclaimer = true;
    TCP_REF(tcp, "posted_reclaimer");
    tcp->memory_owner.PostReclaimer(
        grpc_core::ReclamationPass::kBenign,
        [tcp](absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (sweep.has_value()) {
            perform_reclamation(tcp);
          }
          TCP_UNREF(tcp, "posted_reclaimer");
        });
  }
}

static void tcp_trace_read(grpc_tcp* tcp, grpc_error_handle error)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(tcp->read_mu) {
  grpc_closure* cb = tcp->read_cb;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p call_cb %p %p:%p", tcp, cb, cb->cb, cb->cb_arg);
    size_t i;
    gpr_log(GPR_INFO, "READ %p (peer=%s) error=%s", tcp,
            tcp->peer_string.c_str(), grpc_core::StatusToString(error).c_str());
    if (gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
      for (i = 0; i < tcp->incoming_buffer->count; i++) {
        char* dump = grpc_dump_slice(tcp->incoming_buffer->slices[i],
                                     GPR_DUMP_HEX | GPR_DUMP_ASCII);
        gpr_log(GPR_DEBUG, "READ DATA: %s", dump);
        gpr_free(dump);
      }
    }
  }
}

static void update_rcvlowat(grpc_tcp* tcp)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(tcp->read_mu) {
  if (!grpc_core::IsTcpRcvLowatEnabled()) return;

  // TODO(ctiller): Check if supported by OS.
  // TODO(ctiller): Allow some adjustments instead of hardcoding things.

  static constexpr int kRcvLowatMax = 16 * 1024 * 1024;
  static constexpr int kRcvLowatThreshold = 16 * 1024;

  int remaining = std::min(static_cast<int>(tcp->incoming_buffer->length),
                           tcp->min_progress_size);

  remaining = std::min(remaining, kRcvLowatMax);

  // Setting SO_RCVLOWAT for small quantities does not save on CPU.
  if (remaining < 2 * kRcvLowatThreshold) {
    remaining = 0;
  }

  // Decrement remaining by kRcvLowatThreshold. This would have the effect of
  // waking up a little early. It would help with latency because some bytes
  // may arrive while we execute the recvmsg syscall after waking up.
  if (remaining > 0) {
    remaining -= kRcvLowatThreshold;
  }

  // We still do not know the RPC size. Do not set SO_RCVLOWAT.
  if (tcp->set_rcvlowat <= 1 && remaining <= 1) return;

  // Previous value is still valid. No change needed in SO_RCVLOWAT.
  if (tcp->set_rcvlowat == remaining) {
    return;
  }
  if (setsockopt(tcp->fd, SOL_SOCKET, SO_RCVLOWAT, &remaining,
                 sizeof(remaining)) != 0) {
    gpr_log(GPR_ERROR, "%s",
            absl::StrCat("Cannot set SO_RCVLOWAT on fd=", tcp->fd,
                         " err=", grpc_core::StrError(errno).c_str())
                .c_str());
    return;
  }
  tcp->set_rcvlowat = remaining;
}

// Returns true if data available to read or error other than EAGAIN.
#define MAX_READ_IOVEC 64
static bool tcp_do_read(grpc_tcp* tcp, grpc_error_handle* error)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(tcp->read_mu) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p do_read", tcp);
  }
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t total_read_bytes = 0;
  size_t iov_len =
      std::min<size_t>(MAX_READ_IOVEC, tcp->incoming_buffer->count);
#ifdef GRPC_LINUX_ERRQUEUE
  constexpr size_t cmsg_alloc_space =
      CMSG_SPACE(sizeof(grpc_core::scm_timestamping)) + CMSG_SPACE(sizeof(int));
#else
  constexpr size_t cmsg_alloc_space = 24 /* CMSG_SPACE(sizeof(int)) */;
#endif  // GRPC_LINUX_ERRQUEUE
  char cmsgbuf[cmsg_alloc_space];
  for (size_t i = 0; i < iov_len; i++) {
    iov[i].iov_base = GRPC_SLICE_START_PTR(tcp->incoming_buffer->slices[i]);
    iov[i].iov_len = GRPC_SLICE_LENGTH(tcp->incoming_buffer->slices[i]);
  }

  GPR_ASSERT(tcp->incoming_buffer->length != 0);
  GPR_DEBUG_ASSERT(tcp->min_progress_size > 0);

  do {
    // Assume there is something on the queue. If we receive TCP_INQ from
    // kernel, we will update this value, otherwise, we have to assume there is
    // always something to read until we get EAGAIN.
    tcp->inq = 1;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = static_cast<msg_iovlen_type>(iov_len);
    if (tcp->inq_capable) {
      msg.msg_control = cmsgbuf;
      msg.msg_controllen = sizeof(cmsgbuf);
    } else {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;
    }
    msg.msg_flags = 0;

    grpc_core::global_stats().IncrementTcpReadOffer(
        tcp->incoming_buffer->length);
    grpc_core::global_stats().IncrementTcpReadOfferIovSize(
        tcp->incoming_buffer->count);

    do {
      grpc_core::global_stats().IncrementSyscallRead();
      read_bytes = recvmsg(tcp->fd, &msg, 0);
    } while (read_bytes < 0 && errno == EINTR);

    if (read_bytes < 0 && errno == EAGAIN) {
      // NB: After calling call_read_cb a parallel call of the read handler may
      // be running.
      if (total_read_bytes > 0) {
        break;
      }
      finish_estimate(tcp);
      tcp->inq = 0;
      return false;
    }

    // We have read something in previous reads. We need to deliver those
    // bytes to the upper layer.
    if (read_bytes <= 0 && total_read_bytes >= 1) {
      tcp->inq = 1;
      break;
    }

    if (read_bytes <= 0) {
      // 0 read size ==> end of stream
      grpc_slice_buffer_reset_and_unref(tcp->incoming_buffer);
      if (read_bytes == 0) {
        *error = tcp_annotate_error(absl::InternalError("Socket closed"), tcp);
      } else {
        *error =
            tcp_annotate_error(absl::InternalError(absl::StrCat(
                                   "recvmsg:", grpc_core::StrError(errno))),
                               tcp);
      }
      return true;
    }

    grpc_core::global_stats().IncrementTcpReadSize(read_bytes);
    add_to_estimate(tcp, static_cast<size_t>(read_bytes));
    GPR_DEBUG_ASSERT((size_t)read_bytes <=
                     tcp->incoming_buffer->length - total_read_bytes);

#ifdef GRPC_HAVE_TCP_INQ
    if (tcp->inq_capable) {
      GPR_DEBUG_ASSERT(!(msg.msg_flags & MSG_CTRUNC));
      struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
      for (; cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_TCP && cmsg->cmsg_type == TCP_CM_INQ &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
          tcp->inq = *reinterpret_cast<int*>(CMSG_DATA(cmsg));
          break;
        }
      }
    }
#endif  // GRPC_HAVE_TCP_INQ

    total_read_bytes += read_bytes;
    if (tcp->inq == 0 || total_read_bytes == tcp->incoming_buffer->length) {
      break;
    }

    // We had a partial read, and still have space to read more data.
    // So, adjust IOVs and try to read more.
    size_t remaining = read_bytes;
    size_t j = 0;
    for (size_t i = 0; i < iov_len; i++) {
      if (remaining >= iov[i].iov_len) {
        remaining -= iov[i].iov_len;
        continue;
      }
      if (remaining > 0) {
        iov[j].iov_base = static_cast<char*>(iov[i].iov_base) + remaining;
        iov[j].iov_len = iov[i].iov_len - remaining;
        remaining = 0;
      } else {
        iov[j].iov_base = iov[i].iov_base;
        iov[j].iov_len = iov[i].iov_len;
      }
      ++j;
    }
    iov_len = j;
  } while (true);

  if (tcp->inq == 0) {
    finish_estimate(tcp);
  }

  GPR_DEBUG_ASSERT(total_read_bytes > 0);
  *error = absl::OkStatus();
  if (grpc_core::IsTcpFrameSizeTuningEnabled()) {
    // Update min progress size based on the total number of bytes read in
    // this round.
    tcp->min_progress_size -= total_read_bytes;
    if (tcp->min_progress_size > 0) {
      // There is still some bytes left to be read before we can signal
      // the read as complete. Append the bytes read so far into
      // last_read_buffer which serves as a staging buffer. Return false
      // to indicate tcp_handle_read needs to be scheduled again.
      grpc_slice_buffer_move_first(tcp->incoming_buffer, total_read_bytes,
                                   &tcp->last_read_buffer);
      return false;
    } else {
      // The required number of bytes have been read. Append the bytes
      // read in this round into last_read_buffer. Then swap last_read_buffer
      // and incoming_buffer. Now incoming buffer contains all the bytes
      // read since the start of the last tcp_read operation. last_read_buffer
      // would contain any spare space left in the incoming buffer. This
      // space will be used in the next tcp_read operation.
      tcp->min_progress_size = 1;
      grpc_slice_buffer_move_first(tcp->incoming_buffer, total_read_bytes,
                                   &tcp->last_read_buffer);
      grpc_slice_buffer_swap(&tcp->last_read_buffer, tcp->incoming_buffer);
      return true;
    }
  }
  if (total_read_bytes < tcp->incoming_buffer->length) {
    grpc_slice_buffer_trim_end(tcp->incoming_buffer,
                               tcp->incoming_buffer->length - total_read_bytes,
                               &tcp->last_read_buffer);
  }
  return true;
}

static void maybe_make_read_slices(grpc_tcp* tcp)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(tcp->read_mu) {
  static const int kBigAlloc = 64 * 1024;
  static const int kSmallAlloc = 8 * 1024;
  if (tcp->incoming_buffer->length <
      std::max<size_t>(tcp->min_progress_size, 1)) {
    size_t allocate_length = tcp->min_progress_size;
    const size_t target_length = static_cast<size_t>(tcp->target_length);
    // If memory pressure is low and we think there will be more than
    // min_progress_size bytes to read, allocate a bit more.
    const bool low_memory_pressure =
        tcp->memory_owner.GetPressureInfo().pressure_control_value < 0.8;
    if (low_memory_pressure && target_length > allocate_length) {
      allocate_length = target_length;
    }
    int extra_wanted = std::max<int>(
        1, allocate_length - static_cast<int>(tcp->incoming_buffer->length));
    if (extra_wanted >=
        (low_memory_pressure ? kSmallAlloc * 3 / 2 : kBigAlloc)) {
      while (extra_wanted > 0) {
        extra_wanted -= kBigAlloc;
        grpc_slice_buffer_add_indexed(tcp->incoming_buffer,
                                      tcp->memory_owner.MakeSlice(kBigAlloc));
        grpc_core::global_stats().IncrementTcpReadAlloc64k();
      }
    } else {
      while (extra_wanted > 0) {
        extra_wanted -= kSmallAlloc;
        grpc_slice_buffer_add_indexed(tcp->incoming_buffer,
                                      tcp->memory_owner.MakeSlice(kSmallAlloc));
        grpc_core::global_stats().IncrementTcpReadAlloc8k();
      }
    }
    maybe_post_reclaimer(tcp);
  }
}

static void tcp_handle_read(void* arg /* grpc_tcp */, grpc_error_handle error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p got_read: %s", tcp,
            grpc_core::StatusToString(error).c_str());
  }
  tcp->read_mu.Lock();
  grpc_error_handle tcp_read_error;
  if (GPR_LIKELY(error.ok()) && tcp->memory_owner.is_valid()) {
    maybe_make_read_slices(tcp);
    if (!tcp_do_read(tcp, &tcp_read_error)) {
      // Maybe update rcv lowat value based on the number of bytes read in this
      // round.
      update_rcvlowat(tcp);
      tcp->read_mu.Unlock();
      // We've consumed the edge, request a new one
      notify_on_read(tcp);
      return;
    }
    tcp_trace_read(tcp, tcp_read_error);
  } else {
    if (!tcp->memory_owner.is_valid() && error.ok()) {
      tcp_read_error =
          tcp_annotate_error(absl::InternalError("Socket closed"), tcp);
    } else {
      tcp_read_error = error;
    }
    grpc_slice_buffer_reset_and_unref(tcp->incoming_buffer);
    grpc_slice_buffer_reset_and_unref(&tcp->last_read_buffer);
  }
  // Update rcv lowat needs to be called at the end of the current read
  // operation to ensure the right SO_RCVLOWAT value is set for the next read.
  // Otherwise the next endpoint read operation may get stuck indefinitely
  // because the previously set rcv lowat value will persist and the socket may
  // erroneously considered to not be ready for read.
  update_rcvlowat(tcp);
  grpc_closure* cb = tcp->read_cb;
  tcp->read_cb = nullptr;
  tcp->incoming_buffer = nullptr;
  tcp->read_mu.Unlock();
  grpc_core::Closure::Run(DEBUG_LOCATION, cb, tcp_read_error);
  TCP_UNREF(tcp, "read");
}

static void tcp_read(grpc_endpoint* ep, grpc_slice_buffer* incoming_buffer,
                     grpc_closure* cb, bool urgent, int min_progress_size) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  GPR_ASSERT(tcp->read_cb == nullptr);
  tcp->read_cb = cb;
  tcp->read_mu.Lock();
  tcp->incoming_buffer = incoming_buffer;
  tcp->min_progress_size = grpc_core::IsTcpFrameSizeTuningEnabled()
                               ? std::max(min_progress_size, 1)
                               : 1;
  grpc_slice_buffer_reset_and_unref(incoming_buffer);
  grpc_slice_buffer_swap(incoming_buffer, &tcp->last_read_buffer);
  TCP_REF(tcp, "read");
  if (tcp->is_first_read) {
    tcp->read_mu.Unlock();
    // Endpoint read called for the very first time. Register read callback with
    // the polling engine
    tcp->is_first_read = false;
    notify_on_read(tcp);
  } else if (!urgent && tcp->inq == 0) {
    tcp->read_mu.Unlock();
    // Upper layer asked to read more but we know there is no pending data
    // to read from previous reads. So, wait for POLLIN.
    //
    notify_on_read(tcp);
  } else {
    tcp->read_mu.Unlock();
    // Not the first time. We may or may not have more bytes available. In any
    // case call tcp->read_done_closure (i.e tcp_handle_read()) which does the
    // right thing (i.e calls tcp_do_read() which either reads the available
    // bytes or calls notify_on_read() to be notified when new bytes become
    // available
    grpc_core::Closure::Run(DEBUG_LOCATION, &tcp->read_done_closure,
                            absl::OkStatus());
  }
}

// A wrapper around sendmsg. It sends \a msg over \a fd and returns the number
// of bytes sent.
ssize_t tcp_send(int fd, const struct msghdr* msg, int* saved_errno,
                 int additional_flags = 0) {
  ssize_t sent_length;
  do {
    // TODO(klempner): Cork if this is a partial write
    grpc_core::global_stats().IncrementSyscallWrite();
    sent_length = sendmsg(fd, msg, SENDMSG_FLAGS | additional_flags);
  } while (sent_length < 0 && (*saved_errno = errno) == EINTR);
  return sent_length;
}

/// This is to be called if outgoing_buffer_arg is not null. On linux platforms,
/// this will call sendmsg with socket options set to collect timestamps inside
/// the kernel. On return, sent_length is set to the return value of the sendmsg
/// call. Returns false if setting the socket options failed. This is not
/// implemented for non-linux platforms currently, and crashes out.
///
static bool tcp_write_with_timestamps(grpc_tcp* tcp, struct msghdr* msg,
                                      size_t sending_length,
                                      ssize_t* sent_length, int* saved_errno,
                                      int additional_flags = 0);

/// The callback function to be invoked when we get an error on the socket.
static void tcp_handle_error(void* arg /* grpc_tcp */, grpc_error_handle error);

static TcpZerocopySendRecord* tcp_get_send_zerocopy_record(
    grpc_tcp* tcp, grpc_slice_buffer* buf);

#ifdef GRPC_LINUX_ERRQUEUE
static bool process_errors(grpc_tcp* tcp);

static TcpZerocopySendRecord* tcp_get_send_zerocopy_record(
    grpc_tcp* tcp, grpc_slice_buffer* buf) {
  TcpZerocopySendRecord* zerocopy_send_record = nullptr;
  const bool use_zerocopy =
      tcp->tcp_zerocopy_send_ctx.enabled() &&
      tcp->tcp_zerocopy_send_ctx.threshold_bytes() < buf->length;
  if (use_zerocopy) {
    zerocopy_send_record = tcp->tcp_zerocopy_send_ctx.GetSendRecord();
    if (zerocopy_send_record == nullptr) {
      process_errors(tcp);
      zerocopy_send_record = tcp->tcp_zerocopy_send_ctx.GetSendRecord();
    }
    if (zerocopy_send_record != nullptr) {
      zerocopy_send_record->PrepareForSends(buf);
      GPR_DEBUG_ASSERT(buf->count == 0);
      GPR_DEBUG_ASSERT(buf->length == 0);
      tcp->outgoing_byte_idx = 0;
      tcp->outgoing_buffer = nullptr;
    }
  }
  return zerocopy_send_record;
}

static void ZerocopyDisableAndWaitForRemaining(grpc_tcp* tcp) {
  tcp->tcp_zerocopy_send_ctx.Shutdown();
  while (!tcp->tcp_zerocopy_send_ctx.AllSendRecordsEmpty()) {
    process_errors(tcp);
  }
}

static bool tcp_write_with_timestamps(grpc_tcp* tcp, struct msghdr* msg,
                                      size_t sending_length,
                                      ssize_t* sent_length, int* saved_errno,
                                      int additional_flags) {
  if (!tcp->socket_ts_enabled) {
    uint32_t opt = grpc_core::kTimestampingSocketOptions;
    if (setsockopt(tcp->fd, SOL_SOCKET, SO_TIMESTAMPING,
                   static_cast<void*>(&opt), sizeof(opt)) != 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR, "Failed to set timestamping options on the socket.");
      }
      return false;
    }
    tcp->bytes_counter = -1;
    tcp->socket_ts_enabled = true;
  }
  // Set control message to indicate that you want timestamps.
  union {
    char cmsg_buf[CMSG_SPACE(sizeof(uint32_t))];
    struct cmsghdr align;
  } u;
  cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(u.cmsg_buf);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SO_TIMESTAMPING;
  cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
  *reinterpret_cast<int*>(CMSG_DATA(cmsg)) =
      grpc_core::kTimestampingRecordingOptions;
  msg->msg_control = u.cmsg_buf;
  msg->msg_controllen = CMSG_SPACE(sizeof(uint32_t));

  // If there was an error on sendmsg the logic in tcp_flush will handle it.
  ssize_t length = tcp_send(tcp->fd, msg, saved_errno, additional_flags);
  *sent_length = length;
  // Only save timestamps if all the bytes were taken by sendmsg.
  if (sending_length == static_cast<size_t>(length)) {
    tcp->tb_list.AddNewEntry(static_cast<uint32_t>(tcp->bytes_counter + length),
                             tcp->fd, tcp->outgoing_buffer_arg);
    tcp->outgoing_buffer_arg = nullptr;
  }
  return true;
}

static void UnrefMaybePutZerocopySendRecord(grpc_tcp* tcp,
                                            TcpZerocopySendRecord* record,
                                            uint32_t seq, const char* tag);
// Reads \a cmsg to process zerocopy control messages.
static void process_zerocopy(grpc_tcp* tcp, struct cmsghdr* cmsg) {
  GPR_DEBUG_ASSERT(cmsg);
  auto serr = reinterpret_cast<struct sock_extended_err*>(CMSG_DATA(cmsg));
  GPR_DEBUG_ASSERT(serr->ee_errno == 0);
  GPR_DEBUG_ASSERT(serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY);
  const uint32_t lo = serr->ee_info;
  const uint32_t hi = serr->ee_data;
  for (uint32_t seq = lo; seq <= hi; ++seq) {
    // TODO(arjunroy): It's likely that lo and hi refer to zerocopy sequence
    // numbers that are generated by a single call to grpc_endpoint_write; ie.
    // we can batch the unref operation. So, check if record is the same for
    // both; if so, batch the unref/put.
    TcpZerocopySendRecord* record =
        tcp->tcp_zerocopy_send_ctx.ReleaseSendRecord(seq);
    GPR_DEBUG_ASSERT(record);
    UnrefMaybePutZerocopySendRecord(tcp, record, seq, "CALLBACK RCVD");
  }
  if (tcp->tcp_zerocopy_send_ctx.UpdateZeroCopyOMemStateAfterFree()) {
    grpc_fd_set_writable(tcp->em_fd);
  }
}

// Whether the cmsg received from error queue is of the IPv4 or IPv6 levels.
static bool CmsgIsIpLevel(const cmsghdr& cmsg) {
  return (cmsg.cmsg_level == SOL_IPV6 && cmsg.cmsg_type == IPV6_RECVERR) ||
         (cmsg.cmsg_level == SOL_IP && cmsg.cmsg_type == IP_RECVERR);
}

static bool CmsgIsZeroCopy(const cmsghdr& cmsg) {
  if (!CmsgIsIpLevel(cmsg)) {
    return false;
  }
  auto serr = reinterpret_cast<const sock_extended_err*> CMSG_DATA(&cmsg);
  return serr->ee_errno == 0 && serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY;
}

/// Reads \a cmsg to derive timestamps from the control messages. If a valid
/// timestamp is found, the traced buffer list is updated with this timestamp.
/// The caller of this function should be looping on the control messages found
/// in \a msg. \a cmsg should point to the control message that the caller wants
/// processed.
/// On return, a pointer to a control message is returned. On the next
/// iteration, CMSG_NXTHDR(msg, ret_val) should be passed as \a cmsg.
struct cmsghdr* process_timestamp(grpc_tcp* tcp, msghdr* msg,
                                  struct cmsghdr* cmsg) {
  auto next_cmsg = CMSG_NXTHDR(msg, cmsg);
  cmsghdr* opt_stats = nullptr;
  if (next_cmsg == nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_ERROR, "Received timestamp without extended error");
    }
    return cmsg;
  }

  // Check if next_cmsg is an OPT_STATS msg
  if (next_cmsg->cmsg_level == SOL_SOCKET &&
      next_cmsg->cmsg_type == SCM_TIMESTAMPING_OPT_STATS) {
    opt_stats = next_cmsg;
    next_cmsg = CMSG_NXTHDR(msg, opt_stats);
    if (next_cmsg == nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
        gpr_log(GPR_ERROR, "Received timestamp without extended error");
      }
      return opt_stats;
    }
  }

  if (!(next_cmsg->cmsg_level == SOL_IP || next_cmsg->cmsg_level == SOL_IPV6) ||
      !(next_cmsg->cmsg_type == IP_RECVERR ||
        next_cmsg->cmsg_type == IPV6_RECVERR)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_ERROR, "Unexpected control message");
    }
    return cmsg;
  }

  auto tss =
      reinterpret_cast<struct grpc_core::scm_timestamping*>(CMSG_DATA(cmsg));
  auto serr = reinterpret_cast<struct sock_extended_err*>(CMSG_DATA(next_cmsg));
  if (serr->ee_errno != ENOMSG ||
      serr->ee_origin != SO_EE_ORIGIN_TIMESTAMPING) {
    gpr_log(GPR_ERROR, "Unexpected control message");
    return cmsg;
  }
  tcp->tb_list.ProcessTimestamp(serr, opt_stats, tss);
  return next_cmsg;
}

/// For linux platforms, reads the socket's error queue and processes error
/// messages from the queue.
///
static bool process_errors(grpc_tcp* tcp) {
  bool processed_err = false;
  struct iovec iov;
  iov.iov_base = nullptr;
  iov.iov_len = 0;
  struct msghdr msg;
  msg.msg_name = nullptr;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 0;
  msg.msg_flags = 0;
  // Allocate enough space so we don't need to keep increasing this as size
  // of OPT_STATS increase
  constexpr size_t cmsg_alloc_space =
      CMSG_SPACE(sizeof(grpc_core::scm_timestamping)) +
      CMSG_SPACE(sizeof(sock_extended_err) + sizeof(sockaddr_in)) +
      CMSG_SPACE(32 * NLA_ALIGN(NLA_HDRLEN + sizeof(uint64_t)));
  // Allocate aligned space for cmsgs received along with timestamps
  union {
    char rbuf[cmsg_alloc_space];
    struct cmsghdr align;
  } aligned_buf;
  msg.msg_control = aligned_buf.rbuf;
  int r, saved_errno;
  while (true) {
    msg.msg_controllen = sizeof(aligned_buf.rbuf);
    do {
      r = recvmsg(tcp->fd, &msg, MSG_ERRQUEUE);
      saved_errno = errno;
    } while (r < 0 && saved_errno == EINTR);

    if (r == -1 && saved_errno == EAGAIN) {
      return processed_err;  // No more errors to process
    }
    if (r == -1) {
      return processed_err;
    }
    if (GPR_UNLIKELY((msg.msg_flags & MSG_CTRUNC) != 0)) {
      gpr_log(GPR_ERROR, "Error message was truncated.");
    }

    if (msg.msg_controllen == 0) {
      // There was no control message found. It was probably spurious.
      return processed_err;
    }
    bool seen = false;
    for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg && cmsg->cmsg_len;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      if (CmsgIsZeroCopy(*cmsg)) {
        process_zerocopy(tcp, cmsg);
        seen = true;
        processed_err = true;
      } else if (cmsg->cmsg_level == SOL_SOCKET &&
                 cmsg->cmsg_type == SCM_TIMESTAMPING) {
        cmsg = process_timestamp(tcp, &msg, cmsg);
        seen = true;
        processed_err = true;
      } else {
        // Got a control message that is not a timestamp or zerocopy. Don't know
        // how to handle this.
        if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
          gpr_log(GPR_INFO,
                  "unknown control message cmsg_level:%d cmsg_type:%d",
                  cmsg->cmsg_level, cmsg->cmsg_type);
        }
        return processed_err;
      }
    }
    if (!seen) {
      return processed_err;
    }
  }
}

static void tcp_handle_error(void* arg /* grpc_tcp */,
                             grpc_error_handle error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP:%p got_error: %s", tcp,
            grpc_core::StatusToString(error).c_str());
  }

  if (!error.ok() ||
      static_cast<bool>(gpr_atm_acq_load(&tcp->stop_error_notification))) {
    // We aren't going to register to hear on error anymore, so it is safe to
    // unref.
    TCP_UNREF(tcp, "error-tracking");
    return;
  }

  // We are still interested in collecting timestamps, so let's try reading
  // them.
  bool processed = process_errors(tcp);
  // This might not a timestamps error. Set the read and write closures to be
  // ready.
  if (!processed) {
    grpc_fd_set_readable(tcp->em_fd);
    grpc_fd_set_writable(tcp->em_fd);
  }
  grpc_fd_notify_on_error(tcp->em_fd, &tcp->error_closure);
}

#else   // GRPC_LINUX_ERRQUEUE
static TcpZerocopySendRecord* tcp_get_send_zerocopy_record(
    grpc_tcp* /*tcp*/, grpc_slice_buffer* /*buf*/) {
  return nullptr;
}

static void ZerocopyDisableAndWaitForRemaining(grpc_tcp* /*tcp*/) {}

static bool tcp_write_with_timestamps(grpc_tcp* /*tcp*/, struct msghdr* /*msg*/,
                                      size_t /*sending_length*/,
                                      ssize_t* /*sent_length*/,
                                      int* /* saved_errno */,
                                      int /*additional_flags*/) {
  gpr_log(GPR_ERROR, "Write with timestamps not supported for this platform");
  GPR_ASSERT(0);
  return false;
}

static void tcp_handle_error(void* /*arg*/ /* grpc_tcp */,
                             grpc_error_handle /*error*/) {
  gpr_log(GPR_ERROR, "Error handling is not supported for this platform");
  GPR_ASSERT(0);
}
#endif  // GRPC_LINUX_ERRQUEUE

// If outgoing_buffer_arg is filled, shuts down the list early, so that any
// release operations needed can be performed on the arg
void tcp_shutdown_buffer_list(grpc_tcp* tcp) {
  if (tcp->outgoing_buffer_arg) {
    tcp->tb_list.Shutdown(tcp->outgoing_buffer_arg,
                          GRPC_ERROR_CREATE("TracedBuffer list shutdown"));
    tcp->outgoing_buffer_arg = nullptr;
  }
}

#if defined(IOV_MAX) && IOV_MAX < 260
#define MAX_WRITE_IOVEC IOV_MAX
#else
#define MAX_WRITE_IOVEC 260
#endif
msg_iovlen_type TcpZerocopySendRecord::PopulateIovs(size_t* unwind_slice_idx,
                                                    size_t* unwind_byte_idx,
                                                    size_t* sending_length,
                                                    iovec* iov) {
  msg_iovlen_type iov_size;
  *unwind_slice_idx = out_offset_.slice_idx;
  *unwind_byte_idx = out_offset_.byte_idx;
  for (iov_size = 0;
       out_offset_.slice_idx != buf_.count && iov_size != MAX_WRITE_IOVEC;
       iov_size++) {
    iov[iov_size].iov_base =
        GRPC_SLICE_START_PTR(buf_.slices[out_offset_.slice_idx]) +
        out_offset_.byte_idx;
    iov[iov_size].iov_len =
        GRPC_SLICE_LENGTH(buf_.slices[out_offset_.slice_idx]) -
        out_offset_.byte_idx;
    *sending_length += iov[iov_size].iov_len;
    ++(out_offset_.slice_idx);
    out_offset_.byte_idx = 0;
  }
  GPR_DEBUG_ASSERT(iov_size > 0);
  return iov_size;
}

void TcpZerocopySendRecord::UpdateOffsetForBytesSent(size_t sending_length,
                                                     size_t actually_sent) {
  size_t trailing = sending_length - actually_sent;
  while (trailing > 0) {
    size_t slice_length;
    out_offset_.slice_idx--;
    slice_length = GRPC_SLICE_LENGTH(buf_.slices[out_offset_.slice_idx]);
    if (slice_length > trailing) {
      out_offset_.byte_idx = slice_length - trailing;
      break;
    } else {
      trailing -= slice_length;
    }
  }
}

// returns true if done, false if pending; if returning true, *error is set
static bool do_tcp_flush_zerocopy(grpc_tcp* tcp, TcpZerocopySendRecord* record,
                                  grpc_error_handle* error) {
  msg_iovlen_type iov_size;
  ssize_t sent_length = 0;
  size_t sending_length;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;
  bool tried_sending_message;
  int saved_errno;
  msghdr msg;
  // iov consumes a large space. Keep it as the last item on the stack to
  // improve locality. After all, we expect only the first elements of it being
  // populated in most cases.
  iovec iov[MAX_WRITE_IOVEC];
  while (true) {
    sending_length = 0;
    iov_size = record->PopulateIovs(&unwind_slice_idx, &unwind_byte_idx,
                                    &sending_length, iov);
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;
    msg.msg_flags = 0;
    tried_sending_message = false;
    // Before calling sendmsg (with or without timestamps): we
    // take a single ref on the zerocopy send record.
    tcp->tcp_zerocopy_send_ctx.NoteSend(record);
    saved_errno = 0;
    if (tcp->outgoing_buffer_arg != nullptr) {
      if (!tcp->ts_capable ||
          !tcp_write_with_timestamps(tcp, &msg, sending_length, &sent_length,
                                     &saved_errno, MSG_ZEROCOPY)) {
        // We could not set socket options to collect Fathom timestamps.
        // Fallback on writing without timestamps.
        tcp->ts_capable = false;
        tcp_shutdown_buffer_list(tcp);
      } else {
        tried_sending_message = true;
      }
    }
    if (!tried_sending_message) {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;
      grpc_core::global_stats().IncrementTcpWriteSize(sending_length);
      grpc_core::global_stats().IncrementTcpWriteIovSize(iov_size);
      sent_length = tcp_send(tcp->fd, &msg, &saved_errno, MSG_ZEROCOPY);
    }
    if (tcp->tcp_zerocopy_send_ctx.UpdateZeroCopyOMemStateAfterSend(
            saved_errno == ENOBUFS)) {
      grpc_fd_set_writable(tcp->em_fd);
    }
    if (sent_length < 0) {
      // If this particular send failed, drop ref taken earlier in this method.
      tcp->tcp_zerocopy_send_ctx.UndoSend();
      if (saved_errno == EAGAIN || saved_errno == ENOBUFS) {
        record->UnwindIfThrottled(unwind_slice_idx, unwind_byte_idx);
        return false;
      } else if (saved_errno == EPIPE) {
        *error = tcp_annotate_error(GRPC_OS_ERROR(saved_errno, "sendmsg"), tcp);
        tcp_shutdown_buffer_list(tcp);
        return true;
      } else {
        *error = tcp_annotate_error(GRPC_OS_ERROR(saved_errno, "sendmsg"), tcp);
        tcp_shutdown_buffer_list(tcp);
        return true;
      }
    }
    grpc_core::EventLog::Append("tcp-write-outstanding", -sent_length);
    tcp->bytes_counter += sent_length;
    record->UpdateOffsetForBytesSent(sending_length,
                                     static_cast<size_t>(sent_length));
    if (record->AllSlicesSent()) {
      *error = absl::OkStatus();
      return true;
    }
  }
}

static void UnrefMaybePutZerocopySendRecord(grpc_tcp* tcp,
                                            TcpZerocopySendRecord* record,
                                            uint32_t /*seq*/,
                                            const char* /*tag*/) {
  if (record->Unref()) {
    tcp->tcp_zerocopy_send_ctx.PutSendRecord(record);
  }
}

static bool tcp_flush_zerocopy(grpc_tcp* tcp, TcpZerocopySendRecord* record,
                               grpc_error_handle* error) {
  bool done = do_tcp_flush_zerocopy(tcp, record, error);
  if (done) {
    // Either we encountered an error, or we successfully sent all the bytes.
    // In either case, we're done with this record.
    UnrefMaybePutZerocopySendRecord(tcp, record, 0, "flush_done");
  }
  return done;
}

static bool tcp_flush(grpc_tcp* tcp, grpc_error_handle* error) {
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  msg_iovlen_type iov_size;
  ssize_t sent_length = 0;
  size_t sending_length;
  size_t trailing;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;
  int saved_errno;

  // We always start at zero, because we eagerly unref and trim the slice
  // buffer as we write
  size_t outgoing_slice_idx = 0;

  while (true) {
    sending_length = 0;
    unwind_slice_idx = outgoing_slice_idx;
    unwind_byte_idx = tcp->outgoing_byte_idx;
    for (iov_size = 0; outgoing_slice_idx != tcp->outgoing_buffer->count &&
                       iov_size != MAX_WRITE_IOVEC;
         iov_size++) {
      iov[iov_size].iov_base =
          GRPC_SLICE_START_PTR(
              tcp->outgoing_buffer->slices[outgoing_slice_idx]) +
          tcp->outgoing_byte_idx;
      iov[iov_size].iov_len =
          GRPC_SLICE_LENGTH(tcp->outgoing_buffer->slices[outgoing_slice_idx]) -
          tcp->outgoing_byte_idx;
      sending_length += iov[iov_size].iov_len;
      outgoing_slice_idx++;
      tcp->outgoing_byte_idx = 0;
    }
    GPR_ASSERT(iov_size > 0);

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;
    msg.msg_flags = 0;
    bool tried_sending_message = false;
    saved_errno = 0;
    if (tcp->outgoing_buffer_arg != nullptr) {
      if (!tcp->ts_capable ||
          !tcp_write_with_timestamps(tcp, &msg, sending_length, &sent_length,
                                     &saved_errno)) {
        // We could not set socket options to collect Fathom timestamps.
        // Fallback on writing without timestamps.
        tcp->ts_capable = false;
        tcp_shutdown_buffer_list(tcp);
      } else {
        tried_sending_message = true;
      }
    }
    if (!tried_sending_message) {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;

      grpc_core::global_stats().IncrementTcpWriteSize(sending_length);
      grpc_core::global_stats().IncrementTcpWriteIovSize(iov_size);

      sent_length = tcp_send(tcp->fd, &msg, &saved_errno);
    }

    if (sent_length < 0) {
      if (saved_errno == EAGAIN || saved_errno == ENOBUFS) {
        tcp->outgoing_byte_idx = unwind_byte_idx;
        // unref all and forget about all slices that have been written to this
        // point
        for (size_t idx = 0; idx < unwind_slice_idx; ++idx) {
          grpc_slice_buffer_remove_first(tcp->outgoing_buffer);
        }
        return false;
      } else if (saved_errno == EPIPE) {
        *error = tcp_annotate_error(GRPC_OS_ERROR(saved_errno, "sendmsg"), tcp);
        grpc_slice_buffer_reset_and_unref(tcp->outgoing_buffer);
        tcp_shutdown_buffer_list(tcp);
        return true;
      } else {
        *error = tcp_annotate_error(GRPC_OS_ERROR(saved_errno, "sendmsg"), tcp);
        grpc_slice_buffer_reset_and_unref(tcp->outgoing_buffer);
        tcp_shutdown_buffer_list(tcp);
        return true;
      }
    }

    GPR_ASSERT(tcp->outgoing_byte_idx == 0);
    grpc_core::EventLog::Append("tcp-write-outstanding", -sent_length);
    tcp->bytes_counter += sent_length;
    trailing = sending_length - static_cast<size_t>(sent_length);
    while (trailing > 0) {
      size_t slice_length;

      outgoing_slice_idx--;
      slice_length =
          GRPC_SLICE_LENGTH(tcp->outgoing_buffer->slices[outgoing_slice_idx]);
      if (slice_length > trailing) {
        tcp->outgoing_byte_idx = slice_length - trailing;
        break;
      } else {
        trailing -= slice_length;
      }
    }
    if (outgoing_slice_idx == tcp->outgoing_buffer->count) {
      *error = absl::OkStatus();
      grpc_slice_buffer_reset_and_unref(tcp->outgoing_buffer);
      return true;
    }
  }
}

static void tcp_handle_write(void* arg /* grpc_tcp */,
                             grpc_error_handle error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(arg);
  grpc_closure* cb;

  if (!error.ok()) {
    cb = tcp->write_cb;
    tcp->write_cb = nullptr;
    if (tcp->current_zerocopy_send != nullptr) {
      UnrefMaybePutZerocopySendRecord(tcp, tcp->current_zerocopy_send, 0,
                                      "handle_write_err");
      tcp->current_zerocopy_send = nullptr;
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
    TCP_UNREF(tcp, "write");
    return;
  }
  bool flush_result =
      tcp->current_zerocopy_send != nullptr
          ? tcp_flush_zerocopy(tcp, tcp->current_zerocopy_send, &error)
          : tcp_flush(tcp, &error);
  if (!flush_result) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "write: delayed");
    }
    notify_on_write(tcp);
    // tcp_flush does not populate error if it has returned false.
    GPR_DEBUG_ASSERT(error.ok());
  } else {
    cb = tcp->write_cb;
    tcp->write_cb = nullptr;
    tcp->current_zerocopy_send = nullptr;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "write: %s", grpc_core::StatusToString(error).c_str());
    }
    // No need to take a ref on error since tcp_flush provides a ref.
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
    TCP_UNREF(tcp, "write");
  }
}

static void tcp_write(grpc_endpoint* ep, grpc_slice_buffer* buf,
                      grpc_closure* cb, void* arg, int /*max_frame_size*/) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_error_handle error;
  TcpZerocopySendRecord* zerocopy_send_record = nullptr;

  grpc_core::EventLog::Append("tcp-write-outstanding", buf->length);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    size_t i;

    for (i = 0; i < buf->count; i++) {
      gpr_log(GPR_INFO, "WRITE %p (peer=%s)", tcp, tcp->peer_string.c_str());
      if (gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
        char* data =
            grpc_dump_slice(buf->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
        gpr_log(GPR_DEBUG, "WRITE DATA: %s", data);
        gpr_free(data);
      }
    }
  }

  GPR_ASSERT(tcp->write_cb == nullptr);
  GPR_DEBUG_ASSERT(tcp->current_zerocopy_send == nullptr);

  if (buf->length == 0) {
    grpc_core::Closure::Run(
        DEBUG_LOCATION, cb,
        grpc_fd_is_shutdown(tcp->em_fd)
            ? tcp_annotate_error(GRPC_ERROR_CREATE("EOF"), tcp)
            : absl::OkStatus());
    tcp_shutdown_buffer_list(tcp);
    return;
  }

  zerocopy_send_record = tcp_get_send_zerocopy_record(tcp, buf);
  if (zerocopy_send_record == nullptr) {
    // Either not enough bytes, or couldn't allocate a zerocopy context.
    tcp->outgoing_buffer = buf;
    tcp->outgoing_byte_idx = 0;
  }
  tcp->outgoing_buffer_arg = arg;
  if (arg) {
    GPR_ASSERT(grpc_event_engine_can_track_errors());
  }

  bool flush_result =
      zerocopy_send_record != nullptr
          ? tcp_flush_zerocopy(tcp, zerocopy_send_record, &error)
          : tcp_flush(tcp, &error);
  if (!flush_result) {
    TCP_REF(tcp, "write");
    tcp->write_cb = cb;
    tcp->current_zerocopy_send = zerocopy_send_record;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "write: delayed");
    }
    notify_on_write(tcp);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "write: %s", grpc_core::StatusToString(error).c_str());
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
  }
}

static void tcp_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_pollset_add_fd(pollset, tcp->em_fd);
}

static void tcp_add_to_pollset_set(grpc_endpoint* ep,
                                   grpc_pollset_set* pollset_set) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_pollset_set_add_fd(pollset_set, tcp->em_fd);
}

static void tcp_delete_from_pollset_set(grpc_endpoint* ep,
                                        grpc_pollset_set* pollset_set) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_pollset_set_del_fd(pollset_set, tcp->em_fd);
}

static absl::string_view tcp_get_peer(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  return tcp->peer_string;
}

static absl::string_view tcp_get_local_address(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  return tcp->local_address;
}

static int tcp_get_fd(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  return tcp->fd;
}

static bool tcp_can_track_err(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  if (!grpc_event_engine_can_track_errors()) {
    return false;
  }
  struct sockaddr addr;
  socklen_t len = sizeof(addr);
  if (getsockname(tcp->fd, &addr, &len) < 0) {
    return false;
  }
  return addr.sa_family == AF_INET || addr.sa_family == AF_INET6;
}

static const grpc_endpoint_vtable vtable = {tcp_read,
                                            tcp_write,
                                            tcp_add_to_pollset,
                                            tcp_add_to_pollset_set,
                                            tcp_delete_from_pollset_set,
                                            tcp_shutdown,
                                            tcp_destroy,
                                            tcp_get_peer,
                                            tcp_get_local_address,
                                            tcp_get_fd,
                                            tcp_can_track_err};

grpc_endpoint* grpc_tcp_create(grpc_fd* em_fd,
                               const grpc_core::PosixTcpOptions& options,
                               absl::string_view peer_string) {
  grpc_tcp* tcp = new grpc_tcp(options);
  tcp->base.vtable = &vtable;
  tcp->peer_string = std::string(peer_string);
  tcp->fd = grpc_fd_wrapped_fd(em_fd);
  GPR_ASSERT(options.resource_quota != nullptr);
  tcp->memory_owner =
      options.resource_quota->memory_quota()->CreateMemoryOwner();
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
  // Will be set to false by the very first endpoint read function
  tcp->is_first_read = true;
  tcp->bytes_counter = -1;
  tcp->socket_ts_enabled = false;
  tcp->ts_capable = true;
  tcp->outgoing_buffer_arg = nullptr;
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
  // paired with unref in grpc_tcp_destroy
  new (&tcp->refcount) grpc_core::RefCount(
      1, GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace) ? "tcp" : nullptr);
  gpr_atm_no_barrier_store(&tcp->shutdown_count, 0);
  tcp->em_fd = em_fd;
  grpc_slice_buffer_init(&tcp->last_read_buffer);
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
  // Always assume there is something on the queue to read.
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
#endif  // GRPC_HAVE_TCP_INQ
  // Start being notified on errors if event engine can track errors.
  if (grpc_event_engine_can_track_errors()) {
    // Grab a ref to tcp so that we can safely access the tcp struct when
    // processing errors. We unref when we no longer want to track errors
    // separately.
    TCP_REF(tcp, "error-tracking");
    gpr_atm_rel_store(&tcp->stop_error_notification, 0);
    GRPC_CLOSURE_INIT(&tcp->error_closure, tcp_handle_error, tcp,
                      grpc_schedule_on_exec_ctx);
    grpc_fd_notify_on_error(tcp->em_fd, &tcp->error_closure);
  }

  return &tcp->base;
}

int grpc_tcp_fd(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return grpc_fd_wrapped_fd(tcp->em_fd);
}

void grpc_tcp_destroy_and_release_fd(grpc_endpoint* ep, int* fd,
                                     grpc_closure* done) {
  if (grpc_event_engine::experimental::grpc_is_event_engine_endpoint(ep)) {
    return grpc_event_engine::experimental::
        grpc_event_engine_endpoint_destroy_and_release_fd(ep, fd, done);
  }
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  tcp->release_fd = fd;
  tcp->release_fd_cb = done;
  grpc_slice_buffer_reset_and_unref(&tcp->last_read_buffer);
  if (grpc_event_engine_can_track_errors()) {
    // Stop errors notification.
    ZerocopyDisableAndWaitForRemaining(tcp);
    gpr_atm_no_barrier_store(&tcp->stop_error_notification, true);
    grpc_fd_set_error(tcp->em_fd);
  }
  tcp->read_mu.Lock();
  tcp->memory_owner.Reset();
  tcp->read_mu.Unlock();
  TCP_UNREF(tcp, "destroy");
}

void grpc_tcp_posix_init() { g_backup_poller_mu = new grpc_core::Mutex; }

void grpc_tcp_posix_shutdown() {
  delete g_backup_poller_mu;
  g_backup_poller_mu = nullptr;
}

#endif  // GRPC_POSIX_SOCKET_TCP
