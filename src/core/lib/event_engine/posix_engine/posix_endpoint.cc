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

#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"

#include <atomic>
#include <memory>
#include <unordered_map>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/internal_errqueue.h"

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
#define MAX_READ_IOVEC 4

GPR_GLOBAL_CONFIG_DECLARE_BOOL(grpc_experimental_enable_tcp_frame_size_tuning);

namespace grpc_event_engine {
namespace posix_engine {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;

namespace {

bool ExperimentalTcpFrameSizeTuningEnabled() {
  static const bool kEnableTcpFrameSizeTuning =
      GPR_GLOBAL_CONFIG_GET(grpc_experimental_enable_tcp_frame_size_tuning);
  return kEnableTcpFrameSizeTuning;
}

// A wrapper around sendmsg. It sends \a msg over \a fd and returns the number
// of bytes sent.
ssize_t TcpSend(int fd, const struct msghdr* msg, int* saved_errno,
                int additional_flags = 0) {
  ssize_t sent_length;
  do {
    sent_length = sendmsg(fd, msg, SENDMSG_FLAGS | additional_flags);
  } while (sent_length < 0 && (*saved_errno = errno) == EINTR);
  return sent_length;
}

// Whether the cmsg received from error queue is of the IPv4 or IPv6 levels.
bool CmsgIsIpLevel(const cmsghdr& cmsg) {
  return (cmsg.cmsg_level == SOL_IPV6 && cmsg.cmsg_type == IPV6_RECVERR) ||
         (cmsg.cmsg_level == SOL_IP && cmsg.cmsg_type == IP_RECVERR);
}

bool CmsgIsZeroCopy(const cmsghdr& cmsg) {
  if (!CmsgIsIpLevel(cmsg)) {
    return false;
  }
  auto serr = reinterpret_cast<const sock_extended_err*> CMSG_DATA(&cmsg);
  return serr->ee_errno == 0 && serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY;
}

}  // namespace

class TcpZerocopySendRecord {
 public:
  TcpZerocopySendRecord() { buf_.Clear(); };

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
    // std::cout << "PrepareForSends start\n";
    // fflush(stdout);
    AssertEmpty();
    out_offset_.slice_idx = 0;
    out_offset_.byte_idx = 0;
    buf_.Swap(slices_to_send);
    // std::cout << "PrepareForSends end\n";
    // fflush(stdout);
    Ref();
  }

  // References: 1 reference per sendmsg(), and 1 for the tcp_write().
  void Ref() { ref_.fetch_add(1, std::memory_order_relaxed); }

  // Unref: called when we get an error queue notification for a sendmsg(), if a
  //  sendmsg() failed or when tcp_write() is done.
  bool Unref() {
    const intptr_t prior = ref_.fetch_sub(1, std::memory_order_acq_rel);
    GPR_DEBUG_ASSERT(prior > 0);
    // std::cout << "Unref called\n";
    // fflush(stdout);
    if (prior == 1) {
      // std::cout << "AllSendsComplete set\n";
      // fflush(stdout);
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
       out_offset_.slice_idx != buf_.Count() && iov_size != MAX_WRITE_IOVEC;
       iov_size++) {
    auto slice = buf_.RefSlice(out_offset_.slice_idx);
    iov[iov_size].iov_base =
        const_cast<uint8_t*>(slice.begin()) + out_offset_.byte_idx;
    iov[iov_size].iov_len = slice.length() - out_offset_.byte_idx;
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
    slice_length = buf_.RefSlice(out_offset_.slice_idx).length();
    if (slice_length > trailing) {
      out_offset_.byte_idx = slice_length - trailing;
      break;
    } else {
      trailing -= slice_length;
    }
  }
}

void PosixStreamSocket::AddToEstimate(size_t bytes) {
  bytes_read_this_round_ += static_cast<double>(bytes);
}

void PosixStreamSocket::FinishEstimate() {
  // If we read >80% of the target buffer in one read loop, increase the size of
  // the target buffer to either the amount read, or twice its previous value.
  if (bytes_read_this_round_ > target_length_ * 0.8) {
    target_length_ = std::max(2 * target_length_, bytes_read_this_round_);
  } else {
    target_length_ = 0.99 * target_length_ + 0.01 * bytes_read_this_round_;
  }
  bytes_read_this_round_ = 0;
}

// Returns true if data available to read or error other than EAGAIN.
bool PosixStreamSocket::TcpDoRead(absl::Status& status) {
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t total_read_bytes = 0;
  size_t iov_len = std::min<size_t>(MAX_READ_IOVEC, incoming_buffer_->Count());
#ifdef GRPC_LINUX_ERRQUEUE
  constexpr size_t cmsg_alloc_space =
      CMSG_SPACE(sizeof(scm_timestamping)) + CMSG_SPACE(sizeof(int));
#else
  constexpr size_t cmsg_alloc_space = 24 /* CMSG_SPACE(sizeof(int)) */;
#endif /* GRPC_LINUX_ERRQUEUE */
  char cmsgbuf[cmsg_alloc_space];
  for (size_t i = 0; i < iov_len; i++) {
    Slice slice = incoming_buffer_->RefSlice(i);
    iov[i].iov_base = const_cast<uint8_t*>(slice.begin());
    iov[i].iov_len = slice.length();
  }

  // std::cout << "ENTER TCP do-read: " << std::endl;
  // fflush(stdout);
  GPR_ASSERT(incoming_buffer_->Length() != 0);
  GPR_DEBUG_ASSERT(min_progress_size_ > 0);

  do {
    // Assume there is something on the queue. If we receive TCP_INQ from
    // kernel, we will update this value, otherwise, we have to assume there is
    // always something to read until we get EAGAIN.
    inq_ = 1;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = static_cast<msg_iovlen_type>(iov_len);
    if (inq_capable_) {
      msg.msg_control = cmsgbuf;
      msg.msg_controllen = sizeof(cmsgbuf);
    } else {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;
    }
    msg.msg_flags = 0;

    do {
      read_bytes = recvmsg(fd_, &msg, 0);
    } while (read_bytes < 0 && errno == EINTR);

    // We have read something in previous reads. We need to deliver those bytes
    // to the upper layer.
    if (read_bytes <= 0 &&
        total_read_bytes >= static_cast<size_t>(min_progress_size_)) {
      inq_ = 1;
      break;
    }

    if (read_bytes < 0) {
      // NB: After calling call_read_cb a parallel call of the read handler may
      // be running.
      if (errno == EAGAIN) {
        if (total_read_bytes > 0) {
          break;
        }
        FinishEstimate();
        inq_ = 0;
        return false;
      } else {
        incoming_buffer_->Clear();
        status =
            absl::InternalError(absl::StrCat("recvmsg:", std::strerror(errno)));
        return true;
      }
    }
    if (read_bytes == 0) {
      // 0 read size ==> end of stream
      //
      // We may have read something, i.e., total_read_bytes > 0, but since the
      // connection is closed we will drop the data here, because we can't call
      // the callback multiple times.
      incoming_buffer_->Clear();
      status = absl::InternalError("Socket closed");
      return true;
    }

    AddToEstimate(static_cast<size_t>(read_bytes));
    GPR_DEBUG_ASSERT((size_t)read_bytes <=
                     incoming_buffer_->Length() - total_read_bytes);

#ifdef GRPC_HAVE_TCP_INQ
    if (inq_capable_) {
      GPR_DEBUG_ASSERT(!(msg.msg_flags & MSG_CTRUNC));
      struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
      for (; cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_TCP && cmsg->cmsg_type == TCP_CM_INQ &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
          inq_ = *reinterpret_cast<int*>(CMSG_DATA(cmsg));
          break;
        }
      }
    }
#endif  // GRPC_HAVE_TCP_INQ

    total_read_bytes += read_bytes;
    if (inq_ == 0 || total_read_bytes == incoming_buffer_->Length()) {
      break;
    }

    // We had a partial read, and still have space to read more data. So, adjust
    // IOVs and try to read more.
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

  if (inq_ == 0) {
    FinishEstimate();
  }

  GPR_DEBUG_ASSERT(total_read_bytes > 0);
  status = absl::OkStatus();
  // std::cout << "Frame size tuning enabled: " << frame_size_tuning_enabled_
  //           << " Total read bytes: " << total_read_bytes << std::endl;
  // fflush(stdout);
  if (frame_size_tuning_enabled_) {
    // Update min progress size based on the total number of bytes read in
    // this round.
    min_progress_size_ -= total_read_bytes;
    if (min_progress_size_ > 0) {
      // There is still some bytes left to be read before we can signal
      // the read as complete. Append the bytes read so far into
      // last_read_buffer which serves as a staging buffer. Return false
      // to indicate tcp_handle_read needs to be scheduled again.
      incoming_buffer_->MoveFirstNBytesIntoSliceBuffer(total_read_bytes,
                                                       last_read_buffer_);
      return false;
    } else {
      // The required number of bytes have been read. Append the bytes
      // read in this round into last_read_buffer. Then swap last_read_buffer
      // and incoming_buffer. Now incoming buffer contains all the bytes
      // read since the start of the last tcp_read operation. last_read_buffer
      // would contain any spare space left in the incoming buffer. This
      // space will be used in the next tcp_read operation.
      min_progress_size_ = 1;
      incoming_buffer_->MoveFirstNBytesIntoSliceBuffer(total_read_bytes,
                                                       last_read_buffer_);
      incoming_buffer_->Swap(last_read_buffer_);
      return true;
    }
  }
  if (total_read_bytes < incoming_buffer_->Length()) {
    incoming_buffer_->RemoveLastNBytesIntoSliceBuffer(
        incoming_buffer_->Length() - total_read_bytes, last_read_buffer_);
    // last_read_buffer_.Clear();
  }
  return true;
}

void PosixStreamSocket::PerformReclamation() {
  read_mu_.Lock();
  if (incoming_buffer_ != nullptr) {
    incoming_buffer_->Clear();
  }
  has_posted_reclaimer_ = false;
  read_mu_.Unlock();
}

void PosixStreamSocket::MaybePostReclaimer() {
  if (!has_posted_reclaimer_) {
    has_posted_reclaimer_ = true;
    memory_owner_.PostReclaimer(
        grpc_core::ReclamationPass::kBenign,
        [this](absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (!sweep.has_value()) return;
          PerformReclamation();
        });
  }
}

void PosixStreamSocket::MaybeMakeReadSlices() {
  if (incoming_buffer_->Length() < static_cast<size_t>(min_progress_size_) &&
      incoming_buffer_->Count() < MAX_READ_IOVEC) {
    int target_length =
        std::max(static_cast<int>(target_length_), min_progress_size_);
    int extra_wanted =
        target_length - static_cast<int>(incoming_buffer_->Length());
    int min_read_chunk_size =
        std::max(min_read_chunk_size_, min_progress_size_);
    int max_read_chunk_size =
        std::max(max_read_chunk_size_, min_progress_size_);
    incoming_buffer_->AppendIndexed(
        Slice(memory_owner_.MakeSlice(grpc_core::MemoryRequest(
            min_read_chunk_size,
            grpc_core::Clamp(extra_wanted, min_read_chunk_size,
                             max_read_chunk_size)))));
    // std::cout << "Incoming buffer length = " << incoming_buffer_->Length()
    //           << std::endl;
    MaybePostReclaimer();
  }
}

void PosixStreamSocket::HandleRead(absl::Status status) {
  read_mu_.Lock();
  if (status.ok()) {
    // std::cout << "HandleRead: Making read slices\n";
    // fflush(stdout);
    MaybeMakeReadSlices();
    if (!TcpDoRead(status)) {
      // We've consumed the edge, request a new one.
      read_mu_.Unlock();
      handle_->NotifyOnRead(on_read_);
      return;
    }
  } else {
    incoming_buffer_->Clear();
    last_read_buffer_.Clear();
  }
  absl::AnyInvocable<void(absl::Status)> cb = std::move(read_cb_);
  read_cb_ = nullptr;
  incoming_buffer_ = nullptr;
  read_mu_.Unlock();
  // std::cout << "Calling on_read callback ..." << std::endl;
  // fflush(stdout);
  cb(status);
  // std::cout << "Called on_read callback ..." << std::endl;
  // fflush(stdout);
  Unref();
}

void PosixStreamSocket::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                             SliceBuffer* buffer,
                             const EventEngine::Endpoint::ReadArgs* args) {
  GPR_ASSERT(read_cb_ == nullptr);
  read_mu_.Lock();
  read_cb_ = std::move(on_read);
  incoming_buffer_ = buffer;
  incoming_buffer_->Clear();
  incoming_buffer_->Swap(last_read_buffer_);
  read_mu_.Unlock();
  // std::cout << "Entering Socket Read. Incoming buffer length: "
  //           << incoming_buffer_->Length() << std::endl;
  // fflush(stdout);
  if (args != nullptr && frame_size_tuning_enabled_) {
    min_progress_size_ = args->read_hint_bytes;
  } else {
    min_progress_size_ = 1;
  }
  Ref();
  if (is_first_read_) {
    // Endpoint read called for the very first time. Register read callback
    // with the polling engine.
    is_first_read_ = false;
    // std::cout << "Registering read callback" << std::endl;
    // fflush(stdout);
    handle_->NotifyOnRead(on_read_);
  } else if (inq_ == 0) {
    // Upper layer asked to read more but we know there is no pending data to
    // read from previous reads. So, wait for POLLIN.
    // std::cout << "Registering read callback-2" << std::endl;
    // fflush(stdout);
    handle_->NotifyOnRead(on_read_);
  } else {
    // std::cout << "Calling handle_read directly ..." << std::endl;
    // fflush(stdout);
    on_read_->SetStatus(absl::OkStatus());
    scheduler_->Run(on_read_);
  }
}

#ifdef GRPC_LINUX_ERRQUEUE
TcpZerocopySendRecord* PosixStreamSocket::TcpGetSendZerocopyRecord(
    SliceBuffer& buf) {
  TcpZerocopySendRecord* zerocopy_send_record = nullptr;
  const bool use_zerocopy =
      tcp_zerocopy_send_ctx_->enabled() &&
      tcp_zerocopy_send_ctx_->threshold_bytes() < buf.Length();
  if (use_zerocopy) {
    zerocopy_send_record = tcp_zerocopy_send_ctx_->GetSendRecord();
    if (zerocopy_send_record == nullptr) {
      ProcessErrors();
      zerocopy_send_record = tcp_zerocopy_send_ctx_->GetSendRecord();
    }
    if (zerocopy_send_record != nullptr) {
      zerocopy_send_record->PrepareForSends(buf);
      GPR_DEBUG_ASSERT(buf.Count() == 0);
      GPR_DEBUG_ASSERT(buf.Length() == 0);
      outgoing_byte_idx_ = 0;
      outgoing_buffer_ = nullptr;
    }
  }
  return zerocopy_send_record;
}

// For linux platforms, reads the socket's error queue and processes error
// messages from the queue.
bool PosixStreamSocket::ProcessErrors() {
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
  // Allocate enough space so we don't need to keep increasing this as size of
  // OPT_STATS increase.
  constexpr size_t cmsg_alloc_space =
      CMSG_SPACE(sizeof(scm_timestamping)) +
      CMSG_SPACE(sizeof(sock_extended_err) + sizeof(sockaddr_in)) +
      CMSG_SPACE(32 * NLA_ALIGN(NLA_HDRLEN + sizeof(uint64_t)));
  // Allocate aligned space for cmsgs received along with timestamps.
  union {
    char rbuf[cmsg_alloc_space];
    struct cmsghdr align;
  } aligned_buf;
  msg.msg_control = aligned_buf.rbuf;
  int r, saved_errno;
  while (true) {
    msg.msg_controllen = sizeof(aligned_buf.rbuf);
    do {
      r = recvmsg(fd_, &msg, MSG_ERRQUEUE);
      saved_errno = errno;
    } while (r < 0 && saved_errno == EINTR);

    if (r < 0 && saved_errno == EAGAIN) {
      return processed_err;  // No more errors to process
    } else if (r < 0) {
      return processed_err;
    }
    if (ABSL_PREDICT_FALSE((msg.msg_flags & MSG_CTRUNC) != 0)) {
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
        ProcessZerocopy(cmsg);
        seen = true;
        processed_err = true;
      } else if (cmsg->cmsg_level == SOL_SOCKET &&
                 cmsg->cmsg_type == SCM_TIMESTAMPING) {
        cmsg = ProcessTimestamp(&msg, cmsg);
        seen = true;
        processed_err = true;
      } else {
        // Got a control message that is not a timestamp or zerocopy. Don't know
        // how to handle this.
        return processed_err;
      }
    }
    if (!seen) {
      return processed_err;
    }
  }
}

void PosixStreamSocket::UnrefMaybePutZerocopySendRecord(
    TcpZerocopySendRecord* record) {
  if (record->Unref()) {
    tcp_zerocopy_send_ctx_->PutSendRecord(record);
  }
}

void PosixStreamSocket::ZerocopyDisableAndWaitForRemaining() {
  tcp_zerocopy_send_ctx_->Shutdown();
  while (!tcp_zerocopy_send_ctx_->AllSendRecordsEmpty()) {
    ProcessErrors();
  }
}

// Reads \a cmsg to process zerocopy control messages.
void PosixStreamSocket::ProcessZerocopy(struct cmsghdr* cmsg) {
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
        tcp_zerocopy_send_ctx_->ReleaseSendRecord(seq);
    GPR_DEBUG_ASSERT(record);
    UnrefMaybePutZerocopySendRecord(record);
  }
  if (tcp_zerocopy_send_ctx_->UpdateZeroCopyOMemStateAfterFree()) {
    handle_->SetWritable();
  }
}

// Reads \a cmsg to derive timestamps from the control messages. If a valid
// timestamp is found, the traced buffer list is updated with this timestamp.
// The caller of this function should be looping on the control messages found
// in \a msg. \a cmsg should point to the control message that the caller wants
// processed. On return, a pointer to a control message is returned. On the next
// iteration, CMSG_NXTHDR(msg, ret_val) should be passed as \a cmsg.
struct cmsghdr* PosixStreamSocket::ProcessTimestamp(msghdr* msg,
                                                    struct cmsghdr* cmsg) {
  auto next_cmsg = CMSG_NXTHDR(msg, cmsg);
  cmsghdr* opt_stats = nullptr;
  if (next_cmsg == nullptr) {
    return cmsg;
  }

  // Check if next_cmsg is an OPT_STATS msg.
  if (next_cmsg->cmsg_level == SOL_SOCKET &&
      next_cmsg->cmsg_type == SCM_TIMESTAMPING_OPT_STATS) {
    opt_stats = next_cmsg;
    next_cmsg = CMSG_NXTHDR(msg, opt_stats);
    if (next_cmsg == nullptr) {
      return opt_stats;
    }
  }

  if (!(next_cmsg->cmsg_level == SOL_IP || next_cmsg->cmsg_level == SOL_IPV6) ||
      !(next_cmsg->cmsg_type == IP_RECVERR ||
        next_cmsg->cmsg_type == IPV6_RECVERR)) {
    return cmsg;
  }

  auto tss = reinterpret_cast<scm_timestamping*>(CMSG_DATA(cmsg));
  auto serr = reinterpret_cast<struct sock_extended_err*>(CMSG_DATA(next_cmsg));
  if (serr->ee_errno != ENOMSG ||
      serr->ee_origin != SO_EE_ORIGIN_TIMESTAMPING) {
    gpr_log(GPR_ERROR, "Unexpected control message");
    return cmsg;
  }
  // The error handling can potentially be done on another thread so we need to
  // protect the traced buffer list. A lock free list might be better. Using a
  // simple mutex for now.
  {
    absl::MutexLock lock(&traced_buffer_mu_);
    traced_buffers_.ProcessTimestamp(serr, opt_stats, tss);
  }
  return next_cmsg;
}

void PosixStreamSocket::HandleError(absl::Status status) {
  if (!status.ok() ||
      stop_error_notification_.load(std::memory_order_relaxed)) {
    // We aren't going to register to hear on error anymore, so it is safe to
    // unref.
    Unref();
    return;
  }
  // std::cout << "Handling errors ..." << std::endl;
  // fflush(stdout);

  // We are still interested in collecting timestamps, so let's try reading
  // them.
  if (!ProcessErrors()) {
    // This might not a timestamps error. Set the read and write closures to be
    // ready.
    handle_->SetReadable();
    handle_->SetWritable();
  }
  handle_->NotifyOnError(on_error_);
}

bool PosixStreamSocket::WriteWithTimestamps(struct msghdr* msg,
                                            size_t sending_length,
                                            ssize_t* sent_length,
                                            int* saved_errno,
                                            int additional_flags) {
  if (!socket_ts_enabled_) {
    uint32_t opt = kTimestampingSocketOptions;
    if (setsockopt(fd_, SOL_SOCKET, SO_TIMESTAMPING, static_cast<void*>(&opt),
                   sizeof(opt)) != 0) {
      return false;
    }
    bytes_counter_ = -1;
    socket_ts_enabled_ = true;
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
  *reinterpret_cast<int*>(CMSG_DATA(cmsg)) = kTimestampingRecordingOptions;
  msg->msg_control = u.cmsg_buf;
  msg->msg_controllen = CMSG_SPACE(sizeof(uint32_t));

  // If there was an error on sendmsg the logic in tcp_flush will handle it.
  ssize_t length = TcpSend(fd_, msg, saved_errno, additional_flags);
  *sent_length = length;
  // Only save timestamps if all the bytes were taken by sendmsg.
  if (sending_length == static_cast<size_t>(length)) {
    traced_buffer_mu_.Lock();
    traced_buffers_.AddNewEntry(static_cast<uint32_t>(bytes_counter_ + length),
                                fd_, outgoing_buffer_arg_);
    traced_buffer_mu_.Unlock();
    outgoing_buffer_arg_ = nullptr;
  }
  return true;
}

#else
TcpZerocopySendRecord* PosixStreamSocket::TcpGetSendZerocopyRecord(
    SliceBuffer& /*buf*/) {
  return nullptr;
}

void PosixStreamSocket::HandleError(absl::Status /*status*/) {
  GPR_ASSERT(false && "Error handling not supported on this platform");
}
#endif /* GRPC_LINUX_ERRQUEUE */

// If outgoing_buffer_arg is filled, shuts down the list early, so that any
// release operations needed can be performed on the arg.
void PosixStreamSocket::TcpShutdownTracedBufferList() {
  if (outgoing_buffer_arg_ != nullptr) {
    traced_buffer_mu_.Lock();
    traced_buffers_.Shutdown(outgoing_buffer_arg_,
                             absl::InternalError("TracedBuffer list shutdown"));
    traced_buffer_mu_.Unlock();
    outgoing_buffer_arg_ = nullptr;
  }
}

// returns true if done, false if pending; if returning true, *error is set
bool PosixStreamSocket::DoFlushZerocopy(TcpZerocopySendRecord* record,
                                        absl::Status& status) {
  msg_iovlen_type iov_size;
  ssize_t sent_length = 0;
  size_t sending_length;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;
  bool tried_sending_message;
  int saved_errno;
  msghdr msg;
  status = absl::OkStatus();
  // iov consumes a large space. Keep it as the last item on the stack to
  // improve locality. After all, we expect only the first elements of it
  // being populated in most cases.
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
    tcp_zerocopy_send_ctx_->NoteSend(record);
    saved_errno = 0;
    if (outgoing_buffer_arg_ != nullptr) {
      if (!ts_capable_ ||
          !WriteWithTimestamps(&msg, sending_length, &sent_length, &saved_errno,
                               MSG_ZEROCOPY)) {
        // We could not set socket options to collect Fathom timestamps.
        // Fallback on writing without timestamps.
        ts_capable_ = false;
        TcpShutdownTracedBufferList();
      } else {
        tried_sending_message = true;
      }
    }
    if (!tried_sending_message) {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;
      sent_length = TcpSend(fd_, &msg, &saved_errno, MSG_ZEROCOPY);
    }
    if (tcp_zerocopy_send_ctx_->UpdateZeroCopyOMemStateAfterSend(saved_errno ==
                                                                 ENOBUFS)) {
      handle_->SetWritable();
    }
    if (sent_length < 0) {
      // If this particular send failed, drop ref taken earlier in this method.
      tcp_zerocopy_send_ctx_->UndoSend();
      if (saved_errno == EAGAIN || saved_errno == ENOBUFS) {
        record->UnwindIfThrottled(unwind_slice_idx, unwind_byte_idx);
        // std::cout << "Received EAGAIN or ENOBUFS error" << std::endl;
        // fflush(stdout);
        return false;
      } else {
        status = absl::InternalError(
            absl::StrCat("sendmsg", std::strerror(saved_errno)));
        TcpShutdownTracedBufferList();
        return true;
      }
    }
    bytes_counter_ += sent_length;
    record->UpdateOffsetForBytesSent(sending_length,
                                     static_cast<size_t>(sent_length));
    if (record->AllSlicesSent()) {
      return true;
    }
  }
}

bool PosixStreamSocket::TcpFlushZerocopy(TcpZerocopySendRecord* record,
                                         absl::Status& status) {
  bool done = DoFlushZerocopy(record, status);
  if (done) {
    // Either we encountered an error, or we successfully sent all the bytes.
    // In either case, we're done with this record.
    UnrefMaybePutZerocopySendRecord(record);
  }
  return done;
}

bool PosixStreamSocket::TcpFlush(absl::Status& status) {
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  msg_iovlen_type iov_size;
  ssize_t sent_length = 0;
  size_t sending_length;
  size_t trailing;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;
  int saved_errno;
  status = absl::OkStatus();

  // We always start at zero, because we eagerly unref and trim the slice
  // buffer as we write
  size_t outgoing_slice_idx = 0;

  while (true) {
    sending_length = 0;
    unwind_slice_idx = outgoing_slice_idx;
    unwind_byte_idx = outgoing_byte_idx_;
    for (iov_size = 0; outgoing_slice_idx != outgoing_buffer_->Count() &&
                       iov_size != MAX_WRITE_IOVEC;
         iov_size++) {
      auto slice = outgoing_buffer_->RefSlice(outgoing_slice_idx);
      iov[iov_size].iov_base =
          const_cast<uint8_t*>(slice.begin()) + outgoing_byte_idx_;
      iov[iov_size].iov_len = slice.length() - outgoing_byte_idx_;
      sending_length += iov[iov_size].iov_len;
      outgoing_slice_idx++;
      outgoing_byte_idx_ = 0;
    }
    GPR_ASSERT(iov_size > 0);

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;
    msg.msg_flags = 0;
    bool tried_sending_message = false;
    saved_errno = 0;
    if (outgoing_buffer_arg_ != nullptr) {
      if (!ts_capable_ || !WriteWithTimestamps(&msg, sending_length,
                                               &sent_length, &saved_errno, 0)) {
        // We could not set socket options to collect Fathom timestamps.
        // Fallback on writing without timestamps.
        ts_capable_ = false;
        TcpShutdownTracedBufferList();
      } else {
        tried_sending_message = true;
      }
    }
    if (!tried_sending_message) {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;
      sent_length = TcpSend(fd_, &msg, &saved_errno);
    }

    if (sent_length < 0) {
      if (saved_errno == EAGAIN || saved_errno == ENOBUFS) {
        outgoing_byte_idx_ = unwind_byte_idx;
        // unref all and forget about all slices that have been written to this
        // point
        for (size_t idx = 0; idx < unwind_slice_idx; ++idx) {
          outgoing_buffer_->TakeFirst();
        }
        return false;
      } else {
        status = absl::InternalError(
            absl::StrCat("sendmsg", std::strerror(saved_errno)));
        outgoing_buffer_->Clear();
        TcpShutdownTracedBufferList();
        return true;
      }
    }

    GPR_ASSERT(outgoing_byte_idx_ == 0);
    bytes_counter_ += sent_length;
    trailing = sending_length - static_cast<size_t>(sent_length);
    while (trailing > 0) {
      size_t slice_length;
      outgoing_slice_idx--;
      slice_length = outgoing_buffer_->RefSlice(outgoing_slice_idx).length();
      if (slice_length > trailing) {
        outgoing_byte_idx_ = slice_length - trailing;
        break;
      } else {
        trailing -= slice_length;
      }
    }
    if (outgoing_slice_idx == outgoing_buffer_->Count()) {
      outgoing_buffer_->Clear();
      return true;
    }
  }
}

void PosixStreamSocket::HandleWrite(absl::Status status) {
  if (!status.ok()) {
    absl::AnyInvocable<void(absl::Status)> cb_ = std::move(write_cb_);
    write_cb_ = nullptr;
    if (current_zerocopy_send_ != nullptr) {
      UnrefMaybePutZerocopySendRecord(current_zerocopy_send_);
      current_zerocopy_send_ = nullptr;
    }
    cb_(status);
    Unref();
    return;
  }
  bool flush_result = current_zerocopy_send_ != nullptr
                          ? TcpFlushZerocopy(current_zerocopy_send_, status)
                          : TcpFlush(status);
  if (!flush_result) {
    GPR_DEBUG_ASSERT(status.ok());
    handle_->NotifyOnWrite(on_write_);
  } else {
    absl::AnyInvocable<void(absl::Status)> cb_ = std::move(write_cb_);
    write_cb_ = nullptr;
    current_zerocopy_send_ = nullptr;
    cb_(status);
    Unref();
  }
}

void PosixStreamSocket::Write(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    const EventEngine::Endpoint::WriteArgs* args) {
  absl::Status status = absl::OkStatus();
  TcpZerocopySendRecord* zerocopy_send_record = nullptr;

  GPR_ASSERT(write_cb_ == nullptr);
  GPR_DEBUG_ASSERT(current_zerocopy_send_ == nullptr);
  GPR_DEBUG_ASSERT(data != nullptr);

  if (data->Length() == 0) {
    on_writable(handle_->IsHandleShutdown() ? absl::InternalError("EOF")
                                            : status);
    TcpShutdownTracedBufferList();
    return;
  }

  zerocopy_send_record = TcpGetSendZerocopyRecord(*data);
  if (zerocopy_send_record == nullptr) {
    // Either not enough bytes, or couldn't allocate a zerocopy context.
    outgoing_buffer_ = data;
    outgoing_byte_idx_ = 0;
  }
  if (args != nullptr) {
    outgoing_buffer_arg_ = args->google_specific;
  }
  if (outgoing_buffer_arg_) {
    GPR_ASSERT(poller_->CanTrackErrors());
  }

  bool flush_result = zerocopy_send_record != nullptr
                          ? TcpFlushZerocopy(zerocopy_send_record, status)
                          : TcpFlush(status);
  if (!flush_result) {
    Ref();
    write_cb_ = std::move(on_writable);
    current_zerocopy_send_ = zerocopy_send_record;
    handle_->NotifyOnWrite(on_write_);
  } else {
    on_writable(status);
  }
}

void PosixStreamSocket::MaybeShutdown(absl::Status why) {
  if (poller_->CanTrackErrors()) {
    ZerocopyDisableAndWaitForRemaining();
    stop_error_notification_.store(true, std::memory_order_release);
    handle_->SetHasError();
  }
  handle_->ShutdownHandle(why);
  Unref();
}

PosixStreamSocket ::~PosixStreamSocket() {
  handle_->OrphanHandle(on_done_, nullptr, "");
  delete on_read_;
  delete on_write_;
  delete on_error_;
}

PosixStreamSocket::PosixStreamSocket(EventHandle* handle,
                                     PosixEngineClosure* on_done,
                                     Scheduler* scheduler,
                                     const PosixTcpOptions& options)
    : on_done_(on_done),
      traced_buffers_(),
      handle_(handle),
      poller_(handle->Poller()),
      scheduler_(scheduler) {
  PosixSocketWrapper sock(handle->WrappedFd());
  fd_ = handle_->WrappedFd();
  GPR_ASSERT(options.resource_quota != nullptr);
  memory_owner_ = options.resource_quota->memory_quota()->CreateMemoryOwner(
      *sock.PeerAddressString());
  self_reservation_ = memory_owner_.MakeReservation(sizeof(PosixStreamSocket));
  local_address_ = *sock.LocalAddress();
  peer_address_ = *sock.PeerAddress();
  target_length_ = static_cast<double>(options.tcp_read_chunk_size);
  bytes_read_this_round_ = 0;
  min_read_chunk_size_ = options.tcp_min_read_chunk_size;
  max_read_chunk_size_ = options.tcp_max_read_chunk_size;
  tcp_zerocopy_send_ctx_ = std::make_unique<TcpZerocopySendCtx>(
      options.tcp_tx_zerocopy_max_simultaneous_sends,
      options.tcp_tx_zerocopy_send_bytes_threshold);
  frame_size_tuning_enabled_ = ExperimentalTcpFrameSizeTuningEnabled();
  if (options.tcp_tx_zero_copy_enabled &&
      !tcp_zerocopy_send_ctx_->memory_limited() && poller_->CanTrackErrors()) {
#ifdef GRPC_LINUX_ERRQUEUE
    const int enable = 1;
    auto err =
        setsockopt(fd_, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
    if (err == 0) {
      tcp_zerocopy_send_ctx_->set_enabled(true);
    } else {
      gpr_log(GPR_ERROR, "Failed to set zerocopy options on the socket.");
    }
#endif
  }

#ifdef GRPC_HAVE_TCP_INQ
  int one = 1;
  if (setsockopt(fd_, SOL_TCP, TCP_INQ, &one, sizeof(one)) == 0) {
    inq_capable_ = true;
  } else {
    gpr_log(GPR_DEBUG, "cannot set inq fd=%d errno=%d", fd_, errno);
    inq_capable_ = false;
  }
#else
  inq_capable_ = false;
#endif /* GRPC_HAVE_TCP_INQ */

  on_read_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleRead(std::move(status)); });
  on_write_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleWrite(std::move(status)); });
  on_error_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleError(std::move(status)); });

  // Start being notified on errors if poller can track errors.
  if (poller_->CanTrackErrors()) {
    Ref();
    handle_->NotifyOnError(on_error_);
  }
}

std::unique_ptr<PosixEndpoint> CreatePosixEndpoint(
    EventHandle* handle, PosixEngineClosure* on_shutdown, Scheduler* scheduler,
    const EndpointConfig& config) {
  GPR_ASSERT(handle != nullptr);
  GPR_ASSERT(scheduler != nullptr);
  return std::make_unique<PosixEndpoint>(handle, on_shutdown, scheduler,
                                         config);
}

}  // namespace posix_engine
}  // namespace grpc_event_engine