// Copyright 2022 gRPC Authors
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

#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/internal/slice_cast.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/internal_errqueue.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#ifdef GRPC_LINUX_ERRQUEUE
#include <dirent.h>            // IWYU pragma: keep
#include <linux/capability.h>  // IWYU pragma: keep
#include <linux/errqueue.h>    // IWYU pragma: keep
#include <linux/netlink.h>     // IWYU pragma: keep
#include <sys/prctl.h>         // IWYU pragma: keep
#include <sys/resource.h>      // IWYU pragma: keep
#endif
#include <netinet/in.h>  // IWYU pragma: keep

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

#define MAX_READ_IOVEC 64

namespace grpc_event_engine {
namespace experimental {

namespace {

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

#ifdef GRPC_LINUX_ERRQUEUE

#define CAP_IS_SUPPORTED(cap) (prctl(PR_CAPBSET_READ, (cap), 0) > 0)

// Remove spaces and newline characters from the end of a string.
void rtrim(std::string& s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          s.end());
}

uint64_t ParseUlimitMemLockFromFile(std::string file_name) {
  static std::string kHardMemlockPrefix = "* hard memlock";
  auto result = grpc_core::LoadFile(file_name, false);
  if (!result.ok()) {
    return 0;
  }
  std::string file_contents(reinterpret_cast<const char*>((*result).begin()),
                            (*result).length());
  // Find start position containing prefix.
  size_t start = file_contents.find(kHardMemlockPrefix);
  if (start == std::string::npos) {
    return 0;
  }
  // Find position of next newline after prefix.
  size_t end = file_contents.find(start, '\n');
  // Extract substring between prefix and next newline.
  auto memlock_value_string = file_contents.substr(
      start + kHardMemlockPrefix.length() + 1, end - start);
  rtrim(memlock_value_string);
  if (memlock_value_string == "unlimited" ||
      memlock_value_string == "infinity") {
    return UINT64_MAX;
  } else {
    return std::atoi(memlock_value_string.c_str());
  }
}

// Ulimit hard memlock controls per socket limit for maximum locked memory in
// RAM. Parses all files under  /etc/security/limits.d/ and
// /etc/security/limits.conf file for a line of the following format:
// * hard memlock <value>
// It extracts the first valid <value> and returns it. A value of UINT64_MAX
// represents unlimited or infinity. Hard memlock value should be set to
// allow zerocopy sendmsgs to succeed. It controls the maximum amount of
// memory that can be locked by a socket in RAM.
uint64_t GetUlimitHardMemLock() {
  static const uint64_t kUlimitHardMemLock = []() -> uint64_t {
    if (CAP_IS_SUPPORTED(CAP_SYS_RESOURCE)) {
      // hard memlock ulimit is ignored for privileged user.
      return UINT64_MAX;
    }
    if (auto dir = opendir("/etc/security/limits.d")) {
      while (auto f = readdir(dir)) {
        if (f->d_name[0] == '.') {
          continue;  // Skip everything that starts with a dot
        }
        uint64_t hard_memlock = ParseUlimitMemLockFromFile(
            absl::StrCat("/etc/security/limits.d/", std::string(f->d_name)));
        if (hard_memlock != 0) {
          return hard_memlock;
        }
      }
      closedir(dir);
    }
    return ParseUlimitMemLockFromFile("/etc/security/limits.conf");
  }();
  return kUlimitHardMemLock;
}

// RLIMIT_MEMLOCK controls per process limit for maximum locked memory in RAM.
uint64_t GetRLimitMemLockMax() {
  static const uint64_t kRlimitMemLock = []() -> uint64_t {
    if (CAP_IS_SUPPORTED(CAP_SYS_RESOURCE)) {
      // RLIMIT_MEMLOCK is ignored for privileged user.
      return UINT64_MAX;
    }
    struct rlimit limit;
    if (getrlimit(RLIMIT_MEMLOCK, &limit) != 0) {
      return 0;
    }
    return static_cast<uint64_t>(limit.rlim_max);
  }();
  return kRlimitMemLock;
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
#endif  // GRPC_LINUX_ERRQUEUE

absl::Status PosixOSError(int error_no, const char* call_name) {
  absl::Status s = absl::UnknownError(grpc_core::StrError(error_no));
  grpc_core::StatusSetInt(&s, grpc_core::StatusIntProperty::kErrorNo, error_no);
  grpc_core::StatusSetStr(&s, grpc_core::StatusStrProperty::kOsError,
                          grpc_core::StrError(error_no));
  grpc_core::StatusSetStr(&s, grpc_core::StatusStrProperty::kSyscall,
                          call_name);
  return s;
}

}  // namespace

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
    MutableSlice& slice = internal::SliceCast<MutableSlice>(
        buf_.MutableSliceAt(out_offset_.slice_idx));
    iov[iov_size].iov_base = slice.begin();
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

void PosixEndpointImpl::AddToEstimate(size_t bytes) {
  bytes_read_this_round_ += static_cast<double>(bytes);
}

void PosixEndpointImpl::FinishEstimate() {
  // If we read >80% of the target buffer in one read loop, increase the size of
  // the target buffer to either the amount read, or twice its previous value.
  if (bytes_read_this_round_ > target_length_ * 0.8) {
    target_length_ = std::max(2 * target_length_, bytes_read_this_round_);
  } else {
    target_length_ = 0.99 * target_length_ + 0.01 * bytes_read_this_round_;
  }
  bytes_read_this_round_ = 0;
}

absl::Status PosixEndpointImpl::TcpAnnotateError(absl::Status src_error) {
  auto peer_string = ResolvedAddressToNormalizedString(peer_address_);

  grpc_core::StatusSetStr(&src_error,
                          grpc_core::StatusStrProperty::kTargetAddress,
                          peer_string.ok() ? *peer_string : "");
  grpc_core::StatusSetInt(&src_error, grpc_core::StatusIntProperty::kFd,
                          handle_->WrappedFd());
  grpc_core::StatusSetInt(&src_error, grpc_core::StatusIntProperty::kRpcStatus,
                          GRPC_STATUS_UNAVAILABLE);
  return src_error;
}

// Returns true if data available to read or error other than EAGAIN.
bool PosixEndpointImpl::TcpDoRead(absl::Status& status) {
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t total_read_bytes = 0;
  size_t iov_len = std::min<size_t>(MAX_READ_IOVEC, incoming_buffer_->Count());
#ifdef GRPC_LINUX_ERRQUEUE
  constexpr size_t cmsg_alloc_space =
      CMSG_SPACE(sizeof(scm_timestamping)) + CMSG_SPACE(sizeof(int));
#else
  constexpr size_t cmsg_alloc_space = 24;  // CMSG_SPACE(sizeof(int))
#endif  // GRPC_LINUX_ERRQUEUE
  char cmsgbuf[cmsg_alloc_space];
  for (size_t i = 0; i < iov_len; i++) {
    MutableSlice& slice =
        internal::SliceCast<MutableSlice>(incoming_buffer_->MutableSliceAt(i));
    iov[i].iov_base = slice.begin();
    iov[i].iov_len = slice.length();
  }

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

    if (read_bytes < 0 && errno == EAGAIN) {
      // NB: After calling call_read_cb a parallel call of the read handler may
      // be running.
      if (total_read_bytes > 0) {
        break;
      }
      FinishEstimate();
      inq_ = 0;
      return false;
    }

    // We have read something in previous reads. We need to deliver those bytes
    // to the upper layer.
    if (read_bytes <= 0 && total_read_bytes >= 1) {
      inq_ = 1;
      break;
    }

    if (read_bytes <= 0) {
      // 0 read size ==> end of stream
      incoming_buffer_->Clear();
      if (read_bytes == 0) {
        status = TcpAnnotateError(absl::InternalError("Socket closed"));
      } else {
        status = TcpAnnotateError(absl::InternalError(
            absl::StrCat("recvmsg:", grpc_core::StrError(errno))));
      }
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
  if (grpc_core::IsTcpFrameSizeTuningEnabled()) {
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
    incoming_buffer_->MoveLastNBytesIntoSliceBuffer(
        incoming_buffer_->Length() - total_read_bytes, last_read_buffer_);
  }
  return true;
}

void PosixEndpointImpl::PerformReclamation() {
  read_mu_.Lock();
  if (incoming_buffer_ != nullptr) {
    incoming_buffer_->Clear();
  }
  has_posted_reclaimer_ = false;
  read_mu_.Unlock();
}

void PosixEndpointImpl::MaybePostReclaimer() {
  if (!has_posted_reclaimer_) {
    has_posted_reclaimer_ = true;
    memory_owner_.PostReclaimer(
        grpc_core::ReclamationPass::kBenign,
        [self = Ref(DEBUG_LOCATION, "Posix Reclaimer")](
            absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (sweep.has_value()) {
            self->PerformReclamation();
          }
        });
  }
}

void PosixEndpointImpl::UpdateRcvLowat() {
  if (!grpc_core::IsTcpRcvLowatEnabled()) return;

  // TODO(ctiller): Check if supported by OS.
  // TODO(ctiller): Allow some adjustments instead of hardcoding things.

  static constexpr int kRcvLowatMax = 16 * 1024 * 1024;
  static constexpr int kRcvLowatThreshold = 16 * 1024;

  int remaining = std::min({static_cast<int>(incoming_buffer_->Length()),
                            kRcvLowatMax, min_progress_size_});

  // Setting SO_RCVLOWAT for small quantities does not save on CPU.
  if (remaining < kRcvLowatThreshold) {
    remaining = 0;
  }

  // If zerocopy is off, wake shortly before the full RPC is here. More can
  // show up partway through recvmsg() since it takes a while to copy data.
  // So an early wakeup aids latency.
  if (!tcp_zerocopy_send_ctx_->Enabled() && remaining > 0) {
    remaining -= kRcvLowatThreshold;
  }

  // We still do not know the RPC size. Do not set SO_RCVLOWAT.
  if (set_rcvlowat_ <= 1 && remaining <= 1) return;

  // Previous value is still valid. No change needed in SO_RCVLOWAT.
  if (set_rcvlowat_ == remaining) {
    return;
  }
  auto result = sock_.SetSocketRcvLowat(remaining);
  if (result.ok()) {
    set_rcvlowat_ = *result;
  } else {
    gpr_log(GPR_ERROR, "%s",
            absl::StrCat("ERROR in SO_RCVLOWAT: ", result.status().message())
                .c_str());
  }
}

void PosixEndpointImpl::MaybeMakeReadSlices() {
  static const int kBigAlloc = 64 * 1024;
  static const int kSmallAlloc = 8 * 1024;
  if (incoming_buffer_->Length() < static_cast<size_t>(min_progress_size_)) {
    size_t allocate_length = min_progress_size_;
    const size_t target_length = static_cast<size_t>(target_length_);
    // If memory pressure is low and we think there will be more than
    // min_progress_size bytes to read, allocate a bit more.
    const bool low_memory_pressure =
        memory_owner_.GetPressureInfo().pressure_control_value < 0.8;
    if (low_memory_pressure && target_length > allocate_length) {
      allocate_length = target_length;
    }
    int extra_wanted =
        allocate_length - static_cast<int>(incoming_buffer_->Length());
    if (extra_wanted >=
        (low_memory_pressure ? kSmallAlloc * 3 / 2 : kBigAlloc)) {
      while (extra_wanted > 0) {
        extra_wanted -= kBigAlloc;
        incoming_buffer_->AppendIndexed(
            Slice(memory_owner_.MakeSlice(kBigAlloc)));
      }
    } else {
      while (extra_wanted > 0) {
        extra_wanted -= kSmallAlloc;
        incoming_buffer_->AppendIndexed(
            Slice(memory_owner_.MakeSlice(kSmallAlloc)));
      }
    }
    MaybePostReclaimer();
  }
}

bool PosixEndpointImpl::HandleReadLocked(absl::Status& status) {
  if (status.ok() && memory_owner_.is_valid()) {
    MaybeMakeReadSlices();
    if (!TcpDoRead(status)) {
      UpdateRcvLowat();
      // We've consumed the edge, request a new one.
      return false;
    }
  } else {
    if (!memory_owner_.is_valid() && status.ok()) {
      status = TcpAnnotateError(absl::UnknownError("Shutting down endpoint"));
    }
    incoming_buffer_->Clear();
    last_read_buffer_.Clear();
  }
  return true;
}

void PosixEndpointImpl::HandleRead(absl::Status status) {
  read_mu_.Lock();
  bool ret = false;
  grpc_core::EnsureRunInExecCtx([&, this]() mutable {
    read_mu_.AssertHeld();
    ret = HandleReadLocked(status);
    if (!ret) {
      // The lock needs to be released before ExecCtx Flush because the flush
      // may trigger execution of the reclaimer which may try to acquire the
      // same lock again causing a deadlock.
      read_mu_.Unlock();
    }
  });
  if (!ret) {
    handle_->NotifyOnRead(on_read_);
    return;
  }
  absl::AnyInvocable<void(absl::Status)> cb = std::move(read_cb_);
  read_cb_ = nullptr;
  incoming_buffer_ = nullptr;
  read_mu_.Unlock();
  cb(status);
  Unref();
}

bool PosixEndpointImpl::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                             SliceBuffer* buffer,
                             const EventEngine::Endpoint::ReadArgs* args) {
  grpc_core::ReleasableMutexLock lock(&read_mu_);
  GPR_ASSERT(read_cb_ == nullptr);
  incoming_buffer_ = buffer;
  incoming_buffer_->Clear();
  incoming_buffer_->Swap(last_read_buffer_);
  if (args != nullptr && grpc_core::IsTcpFrameSizeTuningEnabled()) {
    min_progress_size_ = std::max(static_cast<int>(args->read_hint_bytes), 1);
  } else {
    min_progress_size_ = 1;
  }
  Ref().release();
  if (is_first_read_) {
    read_cb_ = std::move(on_read);
    UpdateRcvLowat();
    // Endpoint read called for the very first time. Register read callback
    // with the polling engine.
    is_first_read_ = false;
    lock.Release();
    handle_->NotifyOnRead(on_read_);
  } else if (inq_ == 0) {
    read_cb_ = std::move(on_read);
    UpdateRcvLowat();
    lock.Release();
    // Upper layer asked to read more but we know there is no pending data to
    // read from previous reads. So, wait for POLLIN.
    handle_->NotifyOnRead(on_read_);
  } else {
    absl::Status status;
    MaybeMakeReadSlices();
    if (!TcpDoRead(status)) {
      UpdateRcvLowat();
      read_cb_ = std::move(on_read);
      // We've consumed the edge, request a new one.
      lock.Release();
      handle_->NotifyOnRead(on_read_);
      return false;
    }
    if (!status.ok()) {
      // Read failed immediately. Schedule the on_read callback to run
      // asynchronously.
      lock.Release();
      engine_->Run([on_read = std::move(on_read), status]() mutable {
        on_read(status);
      });
      Unref();
      return false;
    }
    // Read succeeded immediately. Return true and don't run the on_read
    // callback.
    incoming_buffer_ = nullptr;
    Unref();
    return true;
  }
  return false;
}

#ifdef GRPC_LINUX_ERRQUEUE
TcpZerocopySendRecord* PosixEndpointImpl::TcpGetSendZerocopyRecord(
    SliceBuffer& buf) {
  TcpZerocopySendRecord* zerocopy_send_record = nullptr;
  const bool use_zerocopy =
      tcp_zerocopy_send_ctx_->Enabled() &&
      tcp_zerocopy_send_ctx_->ThresholdBytes() < buf.Length();
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
bool PosixEndpointImpl::ProcessErrors() {
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

void PosixEndpointImpl::ZerocopyDisableAndWaitForRemaining() {
  tcp_zerocopy_send_ctx_->Shutdown();
  while (!tcp_zerocopy_send_ctx_->AllSendRecordsEmpty()) {
    ProcessErrors();
  }
}

// Reads \a cmsg to process zerocopy control messages.
void PosixEndpointImpl::ProcessZerocopy(struct cmsghdr* cmsg) {
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
  if (tcp_zerocopy_send_ctx_->UpdateZeroCopyOptMemStateAfterFree()) {
    handle_->SetWritable();
  }
}

// Reads \a cmsg to derive timestamps from the control messages. If a valid
// timestamp is found, the traced buffer list is updated with this timestamp.
// The caller of this function should be looping on the control messages found
// in \a msg. \a cmsg should point to the control message that the caller wants
// processed. On return, a pointer to a control message is returned. On the next
// iteration, CMSG_NXTHDR(msg, ret_val) should be passed as \a cmsg.
struct cmsghdr* PosixEndpointImpl::ProcessTimestamp(msghdr* msg,
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
  traced_buffers_.ProcessTimestamp(serr, opt_stats, tss);
  return next_cmsg;
}

void PosixEndpointImpl::HandleError(absl::Status status) {
  if (!status.ok() ||
      stop_error_notification_.load(std::memory_order_relaxed)) {
    // We aren't going to register to hear on error anymore, so it is safe to
    // unref.
    Unref();
    return;
  }
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

bool PosixEndpointImpl::WriteWithTimestamps(struct msghdr* msg,
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
    traced_buffers_.AddNewEntry(static_cast<uint32_t>(bytes_counter_ + length),
                                fd_, outgoing_buffer_arg_);
    outgoing_buffer_arg_ = nullptr;
  }
  return true;
}

#else   // GRPC_LINUX_ERRQUEUE
TcpZerocopySendRecord* PosixEndpointImpl::TcpGetSendZerocopyRecord(
    SliceBuffer& /*buf*/) {
  return nullptr;
}

void PosixEndpointImpl::HandleError(absl::Status /*status*/) {
  grpc_core::Crash("Error handling not supported on this platform");
}

void PosixEndpointImpl::ZerocopyDisableAndWaitForRemaining() {}

bool PosixEndpointImpl::WriteWithTimestamps(struct msghdr* /*msg*/,
                                            size_t /*sending_length*/,
                                            ssize_t* /*sent_length*/,
                                            int* /*saved_errno*/,
                                            int /*additional_flags*/) {
  grpc_core::Crash("Write with timestamps not supported for this platform");
}
#endif  // GRPC_LINUX_ERRQUEUE

void PosixEndpointImpl::UnrefMaybePutZerocopySendRecord(
    TcpZerocopySendRecord* record) {
  if (record->Unref()) {
    tcp_zerocopy_send_ctx_->PutSendRecord(record);
  }
}

// If outgoing_buffer_arg is filled, shuts down the list early, so that any
// release operations needed can be performed on the arg.
void PosixEndpointImpl::TcpShutdownTracedBufferList() {
  if (outgoing_buffer_arg_ != nullptr) {
    traced_buffers_.Shutdown(outgoing_buffer_arg_,
                             absl::InternalError("TracedBuffer list shutdown"));
    outgoing_buffer_arg_ = nullptr;
  }
}

// returns true if done, false if pending; if returning true, *error is set
bool PosixEndpointImpl::DoFlushZerocopy(TcpZerocopySendRecord* record,
                                        absl::Status& status) {
  msg_iovlen_type iov_size;
  ssize_t sent_length = 0;
  size_t sending_length;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;
  bool tried_sending_message;
  int saved_errno;
  msghdr msg;
  bool constrained;
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
    constrained = false;
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
    if (tcp_zerocopy_send_ctx_->UpdateZeroCopyOptMemStateAfterSend(
            saved_errno == ENOBUFS, constrained) ||
        constrained) {
      // If constrained, is true it implies that we received an ENOBUFS error
      // but there are no un-acked z-copy records. This situation may arise
      // because the per-process RLIMIT_MEMLOCK limit or the per-socket hard
      // memlock ulimit on the machine may be very small. These limits control
      // the max number of bytes a process/socket can respectively pin to RAM.
      // Tx0cp respects these limits and if a sendmsg tries to send more than
      // this limit, the kernel may return ENOBUFS error. Print a warning
      // message here to allow help with debugging. Grpc should not attempt to
      // raise the limit values.
      if (!constrained) {
        handle_->SetWritable();
      } else {
#ifdef GRPC_LINUX_ERRQUEUE
        GRPC_LOG_EVERY_N_SEC(
            1, GPR_INFO,
            "Tx0cp encountered an ENOBUFS error possibly because one or "
            "both of RLIMIT_MEMLOCK or hard memlock ulimit values are too "
            "small for the intended user. Current system value of "
            "RLIMIT_MEMLOCK is %" PRIu64 " and hard memlock ulimit is %" PRIu64
            ".Consider increasing these values appropriately for the intended "
            "user.",
            GetRLimitMemLockMax(), GetUlimitHardMemLock());
#endif
      }
    }
    if (sent_length < 0) {
      // If this particular send failed, drop ref taken earlier in this method.
      tcp_zerocopy_send_ctx_->UndoSend();
      if (saved_errno == EAGAIN || saved_errno == ENOBUFS) {
        record->UnwindIfThrottled(unwind_slice_idx, unwind_byte_idx);
        return false;
      } else {
        status = TcpAnnotateError(PosixOSError(saved_errno, "sendmsg"));
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

bool PosixEndpointImpl::TcpFlushZerocopy(TcpZerocopySendRecord* record,
                                         absl::Status& status) {
  bool done = DoFlushZerocopy(record, status);
  if (done) {
    // Either we encountered an error, or we successfully sent all the bytes.
    // In either case, we're done with this record.
    UnrefMaybePutZerocopySendRecord(record);
  }
  return done;
}

bool PosixEndpointImpl::TcpFlush(absl::Status& status) {
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
      MutableSlice& slice = internal::SliceCast<MutableSlice>(
          outgoing_buffer_->MutableSliceAt(outgoing_slice_idx));
      iov[iov_size].iov_base = slice.begin() + outgoing_byte_idx_;
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
        status = TcpAnnotateError(PosixOSError(saved_errno, "sendmsg"));
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

void PosixEndpointImpl::HandleWrite(absl::Status status) {
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

bool PosixEndpointImpl::Write(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    const EventEngine::Endpoint::WriteArgs* args) {
  absl::Status status = absl::OkStatus();
  TcpZerocopySendRecord* zerocopy_send_record = nullptr;

  GPR_ASSERT(write_cb_ == nullptr);
  GPR_DEBUG_ASSERT(current_zerocopy_send_ == nullptr);
  GPR_DEBUG_ASSERT(data != nullptr);

  if (data->Length() == 0) {
    TcpShutdownTracedBufferList();
    if (handle_->IsHandleShutdown()) {
      status = TcpAnnotateError(absl::InternalError("EOF"));
      engine_->Run([on_writable = std::move(on_writable), status]() mutable {
        on_writable(status);
      });
      return false;
    }
    return true;
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
    Ref().release();
    write_cb_ = std::move(on_writable);
    current_zerocopy_send_ = zerocopy_send_record;
    handle_->NotifyOnWrite(on_write_);
    return false;
  }
  if (!status.ok()) {
    // Write failed immediately. Schedule the on_writable callback to run
    // asynchronously.
    engine_->Run([on_writable = std::move(on_writable), status]() mutable {
      on_writable(status);
    });
    return false;
  }
  // Write succeeded immediately. Return true and don't run the on_writable
  // callback.
  return true;
}

void PosixEndpointImpl::MaybeShutdown(
    absl::Status why,
    absl::AnyInvocable<void(absl::StatusOr<int>)> on_release_fd) {
  if (poller_->CanTrackErrors()) {
    ZerocopyDisableAndWaitForRemaining();
    stop_error_notification_.store(true, std::memory_order_release);
    handle_->SetHasError();
  }
  on_release_fd_ = std::move(on_release_fd);
  grpc_core::StatusSetInt(&why, grpc_core::StatusIntProperty::kRpcStatus,
                          GRPC_STATUS_UNAVAILABLE);
  handle_->ShutdownHandle(why);
  read_mu_.Lock();
  memory_owner_.Reset();
  read_mu_.Unlock();
  Unref();
}

PosixEndpointImpl ::~PosixEndpointImpl() {
  int release_fd = -1;
  handle_->OrphanHandle(on_done_,
                        on_release_fd_ == nullptr ? nullptr : &release_fd, "");
  if (on_release_fd_ != nullptr) {
    engine_->Run([on_release_fd = std::move(on_release_fd_),
                  release_fd]() mutable { on_release_fd(release_fd); });
  }
  delete on_read_;
  delete on_write_;
  delete on_error_;
}

PosixEndpointImpl::PosixEndpointImpl(EventHandle* handle,
                                     PosixEngineClosure* on_done,
                                     std::shared_ptr<EventEngine> engine,
                                     MemoryAllocator&& /*allocator*/,
                                     const PosixTcpOptions& options)
    : sock_(PosixSocketWrapper(handle->WrappedFd())),
      on_done_(on_done),
      traced_buffers_(),
      handle_(handle),
      poller_(handle->Poller()),
      engine_(engine) {
  PosixSocketWrapper sock(handle->WrappedFd());
  fd_ = handle_->WrappedFd();
  GPR_ASSERT(options.resource_quota != nullptr);
  auto peer_addr_string = sock.PeerAddressString();
  mem_quota_ = options.resource_quota->memory_quota();
  memory_owner_ = mem_quota_->CreateMemoryOwner(
      peer_addr_string.ok() ? *peer_addr_string : "");
  self_reservation_ = memory_owner_.MakeReservation(sizeof(PosixEndpointImpl));
  auto local_address = sock.LocalAddress();
  if (local_address.ok()) {
    local_address_ = *local_address;
  }
  auto peer_address = sock.PeerAddress();
  if (peer_address.ok()) {
    peer_address_ = *peer_address;
  }
  target_length_ = static_cast<double>(options.tcp_read_chunk_size);
  bytes_read_this_round_ = 0;
  min_read_chunk_size_ = options.tcp_min_read_chunk_size;
  max_read_chunk_size_ = options.tcp_max_read_chunk_size;
  bool zerocopy_enabled =
      options.tcp_tx_zero_copy_enabled && poller_->CanTrackErrors();
#ifdef GRPC_LINUX_ERRQUEUE
  if (zerocopy_enabled) {
    if (GetRLimitMemLockMax() == 0) {
      zerocopy_enabled = false;
      gpr_log(
          GPR_ERROR,
          "Tx zero-copy will not be used by gRPC since RLIMIT_MEMLOCK value is "
          "not set. Consider raising its value with setrlimit().");
    } else if (GetUlimitHardMemLock() == 0) {
      zerocopy_enabled = false;
      gpr_log(GPR_ERROR,
              "Tx zero-copy will not be used by gRPC since hard memlock ulimit "
              "value is not set. Use ulimit -l <value> to set its value.");
    } else {
      const int enable = 1;
      if (setsockopt(fd_, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable)) !=
          0) {
        zerocopy_enabled = false;
        gpr_log(GPR_ERROR, "Failed to set zerocopy options on the socket.");
      }
    }

    if (zerocopy_enabled) {
      gpr_log(GPR_INFO,
              "Tx-zero copy enabled for gRPC sends. RLIMIT_MEMLOCK value = "
              "%" PRIu64 ",ulimit hard memlock value = %" PRIu64,
              GetRLimitMemLockMax(), GetUlimitHardMemLock());
    }
  }
#endif  // GRPC_LINUX_ERRQUEUE
  tcp_zerocopy_send_ctx_ = std::make_unique<TcpZerocopySendCtx>(
      zerocopy_enabled, options.tcp_tx_zerocopy_max_simultaneous_sends,
      options.tcp_tx_zerocopy_send_bytes_threshold);
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
#endif  // GRPC_HAVE_TCP_INQ

  on_read_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleRead(std::move(status)); });
  on_write_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleWrite(std::move(status)); });
  on_error_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleError(std::move(status)); });

  // Start being notified on errors if poller can track errors.
  if (poller_->CanTrackErrors()) {
    Ref().release();
    handle_->NotifyOnError(on_error_);
  }
}

std::unique_ptr<PosixEndpoint> CreatePosixEndpoint(
    EventHandle* handle, PosixEngineClosure* on_shutdown,
    std::shared_ptr<EventEngine> engine, MemoryAllocator&& allocator,
    const PosixTcpOptions& options) {
  GPR_DEBUG_ASSERT(handle != nullptr);
  return std::make_unique<PosixEndpoint>(handle, on_shutdown, std::move(engine),
                                         std::move(allocator), options);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#else  // GRPC_POSIX_SOCKET_TCP

namespace grpc_event_engine {
namespace experimental {

std::unique_ptr<PosixEndpoint> CreatePosixEndpoint(
    EventHandle* /*handle*/, PosixEngineClosure* /*on_shutdown*/,
    std::shared_ptr<EventEngine> /*engine*/,
    const PosixTcpOptions& /*options*/) {
  grpc_core::Crash("Cannot create PosixEndpoint on this platform");
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_POSIX_SOCKET_TCP
