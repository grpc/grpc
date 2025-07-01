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

#include "src/core/lib/event_engine/posix_engine/traced_buffer_list.h"

#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"
#include "src/core/util/sync.h"

#ifdef GRPC_LINUX_ERRQUEUE
#include <linux/errqueue.h>  // IWYU pragma: keep
#include <linux/netlink.h>
#include <sys/socket.h>  // IWYU pragma: keep

namespace grpc_event_engine::experimental {

namespace {

// Used to extract individual opt stats from cmsg, so as to avoid troubles with
// unaligned reads.
template <typename T>
T ReadUnaligned(const void* ptr) {
  T val;
  memcpy(&val, ptr, sizeof(val));
  return val;
}

// Extracts opt stats from the tcp_info struct \a info to \a metrics
PosixWriteEventSink::ConnectionMetrics ExtractOptStatsFromTcpInfo(
    const tcp_info* info) {
  PosixWriteEventSink::ConnectionMetrics metrics;
  if (info == nullptr) {
    return metrics;
  }
  if (info->length > offsetof(tcp_info, tcpi_sndbuf_limited)) {
    metrics.recurring_retrans = info->tcpi_retransmits;
    metrics.is_delivery_rate_app_limited = info->tcpi_delivery_rate_app_limited;
    metrics.congestion_window = info->tcpi_snd_cwnd;
    metrics.reordering = info->tcpi_reordering;
    metrics.packet_retx = info->tcpi_total_retrans;
    metrics.pacing_rate = info->tcpi_pacing_rate;
    metrics.data_notsent = info->tcpi_notsent_bytes;
    if (info->tcpi_min_rtt != UINT32_MAX) {
      metrics.min_rtt = info->tcpi_min_rtt;
    }
    metrics.packet_sent = info->tcpi_data_segs_out;
    metrics.delivery_rate = info->tcpi_delivery_rate;
    metrics.busy_usec = info->tcpi_busy_time;
    metrics.rwnd_limited_usec = info->tcpi_rwnd_limited;
    metrics.sndbuf_limited_usec = info->tcpi_sndbuf_limited;
  }
  if (info->length > offsetof(tcp_info, tcpi_dsack_dups)) {
    metrics.data_sent = info->tcpi_bytes_sent;
    metrics.data_retx = info->tcpi_bytes_retrans;
    metrics.packet_spurious_retx = info->tcpi_dsack_dups;
  }
  return metrics;
}

// Extracts opt stats from the given control message \a opt_stats to the
// connection metrics \a metrics.
PosixWriteEventSink::ConnectionMetrics ExtractOptStatsFromCmsg(
    const cmsghdr* opt_stats) {
  PosixWriteEventSink::ConnectionMetrics metrics;
  if (opt_stats == nullptr) {
    return metrics;
  }
  const auto* data = CMSG_DATA(opt_stats);
  constexpr int64_t cmsg_hdr_len = CMSG_ALIGN(sizeof(struct cmsghdr));
  const int64_t len = opt_stats->cmsg_len - cmsg_hdr_len;
  int64_t offset = 0;
  while (offset < len) {
    const auto* attr = reinterpret_cast<const nlattr*>(data + offset);
    const void* val = data + offset + NLA_HDRLEN;
    switch (attr->nla_type) {
      case TCP_NLA_BUSY: {
        metrics.busy_usec = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_RWND_LIMITED: {
        metrics.rwnd_limited_usec = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_SNDBUF_LIMITED: {
        metrics.sndbuf_limited_usec = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_PACING_RATE: {
        metrics.pacing_rate = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DELIVERY_RATE: {
        metrics.delivery_rate = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DELIVERY_RATE_APP_LMT: {
        metrics.is_delivery_rate_app_limited = ReadUnaligned<uint8_t>(val);
        break;
      }
      case TCP_NLA_SND_CWND: {
        metrics.congestion_window = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_MIN_RTT: {
        metrics.min_rtt = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_SRTT: {
        metrics.srtt = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_RECUR_RETRANS: {
        metrics.recurring_retrans = ReadUnaligned<uint8_t>(val);
        break;
      }
      case TCP_NLA_BYTES_SENT: {
        metrics.data_sent = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DATA_SEGS_OUT: {
        metrics.packet_sent = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_TOTAL_RETRANS: {
        metrics.packet_retx = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DELIVERED: {
        metrics.packet_delivered = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_DELIVERED_CE: {
        metrics.packet_delivered_ce = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_BYTES_RETRANS: {
        metrics.data_retx = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DSACK_DUPS: {
        metrics.packet_spurious_retx = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_REORDERING: {
        metrics.reordering = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_SND_SSTHRESH: {
        metrics.snd_ssthresh = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_BYTES_NOTSENT: {
        metrics.data_notsent = ReadUnaligned<uint16_t>(val);
        break;
      }
    }
    offset += NLA_ALIGN(attr->nla_len);
  }
  return metrics;
}

grpc_core::Duration g_max_pending_ack_time = grpc_core::Duration::Seconds(10);

}  // namespace.

bool TracedBufferList::TracedBuffer::TimedOut(grpc_core::Timestamp now) {
  return last_timestamp_ + g_max_pending_ack_time < now;
}

void TracedBufferList::AddNewEntry(int32_t seq_no,
                                   EventEnginePosixInterface* posix_interface,
                                   const FileDescriptor& fd,
                                   EventEngine::Endpoint::WriteEventSink sink) {
  TracedBuffer new_elem(seq_no, std::move(sink));
  // Store the current time as the sendmsg time.
  // new_elem.ts_.sendmsg_time.time = gpr_now(GPR_CLOCK_REALTIME);
  auto curr_time = absl::Now();
  struct tcp_info info;
  if (posix_interface != nullptr &&
      GetSocketTcpInfo(&info, posix_interface, fd).ok()) {
    new_elem.sink_.RecordEvent(EventEngine::Endpoint::WriteEvent::kSendMsg,
                               curr_time, ExtractOptStatsFromTcpInfo(&info));
  } else {
    new_elem.sink_.RecordEvent(EventEngine::Endpoint::WriteEvent::kSendMsg,
                               curr_time,
                               PosixWriteEventSink::ConnectionMetrics());
  }
  new_elem.last_timestamp_ = grpc_core::Timestamp::Now();
  // new_elem.last_timestamp_ = new_elem.ts_.sendmsg_time.time;
  grpc_core::MutexLock lock(&mu_);
  list_.push_back(std::move(new_elem));
}

void TracedBufferList::ProcessTimestamp(struct sock_extended_err* serr,
                                        struct cmsghdr* opt_stats,
                                        struct scm_timestamping* tss) {
  absl::Time timestamp = absl::TimeFromTimespec(tss->ts[0]);
  grpc_core::Timestamp core_timestamp = grpc_core::Timestamp::Now();
  auto metrics = ExtractOptStatsFromCmsg(opt_stats);
  grpc_core::MutexLock lock(&mu_);
  auto it = list_.begin();
  while (it != list_.end()) {
    // The byte number refers to the sequence number of the last byte which this
    // timestamp relates to.
    if (serr->ee_data >= it->seq_no_) {
      switch (serr->ee_info) {
        case SCM_TSTAMP_SCHED:
          it->sink_.RecordEvent(EventEngine::Endpoint::WriteEvent::kScheduled,
                                timestamp, metrics);
          it->last_timestamp_ = core_timestamp;
          ++it;
          break;
        case SCM_TSTAMP_SND:
          it->sink_.RecordEvent(EventEngine::Endpoint::WriteEvent::kSent,
                                timestamp, metrics);
          it->last_timestamp_ = core_timestamp;
          ++it;
          break;
        case SCM_TSTAMP_ACK:
          it->sink_.RecordEvent(EventEngine::Endpoint::WriteEvent::kAcked,
                                timestamp, metrics);
          it = list_.erase(it);
          break;
        default:
          grpc_core::Crash(
              absl::StrCat("Unknown timestamp type %d", serr->ee_info));
      }
    } else {
      break;
    }
  }

  it = list_.begin();
  while (it != list_.end()) {
    if (!it->TimedOut(core_timestamp)) {
      ++it;
      continue;
    } else {
      LOG(ERROR) << "No timestamp received for TracedBuffer in "
                 << g_max_pending_ack_time << ". Removing.";
      it = list_.erase(it);
    }
  }
}

void TracedBufferList::Shutdown(
    std::optional<EventEngine::Endpoint::WriteEventSink> remaining) {
  if (remaining.has_value()) {
    PosixWriteEventSink sink(std::move(remaining).value());
    sink.RecordEvent(EventEngine::Endpoint::WriteEvent::kClosed, absl::Now(),
                     PosixWriteEventSink::ConnectionMetrics());
  }
  grpc_core::MutexLock lock(&mu_);
  if (list_.empty()) return;
  auto curr_time = absl::Now();
  for (auto it = list_.begin(); it != list_.end(); ++it) {
    it->sink_.RecordEvent(EventEngine::Endpoint::WriteEvent::kClosed, curr_time,
                          PosixWriteEventSink::ConnectionMetrics());
  }
  list_.clear();
}

void TracedBufferList::TestOnlySetMaxPendingAckTime(
    grpc_core::Duration duration) {
  g_max_pending_ack_time = duration;
}

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_LINUX_ERRQUEUE
