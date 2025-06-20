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
// Fills gpr_timespec gts based on values from timespec ts.
void FillGprFromTimestamp(gpr_timespec* gts, const struct timespec* ts) {
  gts->tv_sec = ts->tv_sec;
  gts->tv_nsec = static_cast<int32_t>(ts->tv_nsec);
  gts->clock_type = GPR_CLOCK_REALTIME;
}

// Used to extract individual opt stats from cmsg, so as to avoid troubles with
// unaligned reads.
template <typename T>
T ReadUnaligned(const void* ptr) {
  T val;
  memcpy(&val, ptr, sizeof(val));
  return val;
}

// Extracts opt stats from the tcp_info struct \a info to \a metrics
void ExtractOptStatsFromTcpInfo(ConnectionMetrics* metrics,
                                const tcp_info* info) {
  if (info == nullptr) {
    return;
  }
  if (info->length > offsetof(tcp_info, tcpi_sndbuf_limited)) {
    metrics->recurring_retrans = info->tcpi_retransmits;
    metrics->is_delivery_rate_app_limited =
        info->tcpi_delivery_rate_app_limited;
    metrics->congestion_window = info->tcpi_snd_cwnd;
    metrics->reordering = info->tcpi_reordering;
    metrics->packet_retx = info->tcpi_total_retrans;
    metrics->pacing_rate = info->tcpi_pacing_rate;
    metrics->data_notsent = info->tcpi_notsent_bytes;
    if (info->tcpi_min_rtt != UINT32_MAX) {
      metrics->min_rtt = info->tcpi_min_rtt;
    }
    metrics->packet_sent = info->tcpi_data_segs_out;
    metrics->delivery_rate = info->tcpi_delivery_rate;
    metrics->busy_usec = info->tcpi_busy_time;
    metrics->rwnd_limited_usec = info->tcpi_rwnd_limited;
    metrics->sndbuf_limited_usec = info->tcpi_sndbuf_limited;
  }
  if (info->length > offsetof(tcp_info, tcpi_dsack_dups)) {
    metrics->data_sent = info->tcpi_bytes_sent;
    metrics->data_retx = info->tcpi_bytes_retrans;
    metrics->packet_spurious_retx = info->tcpi_dsack_dups;
  }
}

// Extracts opt stats from the given control message \a opt_stats to the
// connection metrics \a metrics.
void ExtractOptStatsFromCmsg(ConnectionMetrics* metrics,
                             const cmsghdr* opt_stats) {
  if (opt_stats == nullptr) {
    return;
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
        metrics->busy_usec = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_RWND_LIMITED: {
        metrics->rwnd_limited_usec = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_SNDBUF_LIMITED: {
        metrics->sndbuf_limited_usec = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_PACING_RATE: {
        metrics->pacing_rate = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DELIVERY_RATE: {
        metrics->delivery_rate = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DELIVERY_RATE_APP_LMT: {
        metrics->is_delivery_rate_app_limited = ReadUnaligned<uint8_t>(val);
        break;
      }
      case TCP_NLA_SND_CWND: {
        metrics->congestion_window = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_MIN_RTT: {
        metrics->min_rtt = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_SRTT: {
        metrics->srtt = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_RECUR_RETRANS: {
        metrics->recurring_retrans = ReadUnaligned<uint8_t>(val);
        break;
      }
      case TCP_NLA_BYTES_SENT: {
        metrics->data_sent = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DATA_SEGS_OUT: {
        metrics->packet_sent = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_TOTAL_RETRANS: {
        metrics->packet_retx = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DELIVERED: {
        metrics->packet_delivered = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_DELIVERED_CE: {
        metrics->packet_delivered_ce = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_BYTES_RETRANS: {
        metrics->data_retx = ReadUnaligned<uint64_t>(val);
        break;
      }
      case TCP_NLA_DSACK_DUPS: {
        metrics->packet_spurious_retx = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_REORDERING: {
        metrics->reordering = ReadUnaligned<uint32_t>(val);
        break;
      }
      case TCP_NLA_SND_SSTHRESH: {
        metrics->snd_ssthresh = ReadUnaligned<uint32_t>(val);
        break;
      }
    }
    offset += NLA_ALIGN(attr->nla_len);
  }
}
}  // namespace.

bool TracedBufferList::TracedBuffer::Finished(gpr_timespec ts) {
  constexpr int kGrpcMaxPendingAckTimeMillis = 10000;
  return gpr_time_to_millis(gpr_time_sub(ts, last_timestamp_)) >
         kGrpcMaxPendingAckTimeMillis;
}

void TracedBufferList::AddNewEntry(int32_t seq_no,
                                   EventEnginePosixInterface* posix_interface,
                                   const FileDescriptor& fd,
                                   EventEngine::Endpoint::WriteEventSink sink) {
  TracedBuffer new_elem(seq_no, std::move(sink));
  // Store the current time as the sendmsg time.
  // new_elem.ts_.sendmsg_time.time = gpr_now(GPR_CLOCK_REALTIME);
  // new_elem.ts_.scheduled_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  // new_elem.ts_.sent_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  // new_elem.ts_.acked_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  // if (GetSocketTcpInfo(&(new_elem.ts_.info), posix_interface, fd).ok()) {
  //   ExtractOptStatsFromTcpInfo(&(new_elem.ts_.sendmsg_time.metrics),
  //                              &(new_elem.ts_.info));
  // }
  // new_elem.last_timestamp_ = new_elem.ts_.sendmsg_time.time;
  grpc_core::MutexLock lock(&mu_);
  list_.push_back(std::move(new_elem));
}

void TracedBufferList::ProcessTimestamp(struct sock_extended_err* serr,
                                        struct cmsghdr* opt_stats,
                                        struct scm_timestamping* tss) {
  grpc_core::MutexLock lock(&mu_);
  auto it = list_.begin();
  while (it != list_.end()) {
    // The byte number refers to the sequence number of the last byte which this
    // timestamp relates to.
    if (serr->ee_data >= it->seq_no_) {
      switch (serr->ee_info) {
        case SCM_TSTAMP_SCHED:
          FillGprFromTimestamp(&(it->ts_.scheduled_time.time), &(tss->ts[0]));
          ExtractOptStatsFromCmsg(&(it->ts_.scheduled_time.metrics), opt_stats);
          it->last_timestamp_ = it->ts_.scheduled_time.time;
          ++it;
          break;
        case SCM_TSTAMP_SND:
          FillGprFromTimestamp(&(it->ts_.sent_time.time), &(tss->ts[0]));
          ExtractOptStatsFromCmsg(&(it->ts_.sent_time.metrics), opt_stats);
          it->last_timestamp_ = it->ts_.sent_time.time;
          ++it;
          break;
        case SCM_TSTAMP_ACK:
          FillGprFromTimestamp(&(it->ts_.acked_time.time), &(tss->ts[0]));
          ExtractOptStatsFromCmsg(&(it->ts_.acked_time.metrics), opt_stats);
          // it->sink_.TakeEventCallback()();
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
  // gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  // for (it = list_.begin(); it != list_.end(); ++it) {
  //   if (!it->Finished(gpr_now(GPR_CLOCK_REALTIME))) {
  //     break;
  //   }
  // }
  // elem = head_;
  // gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  // while (elem != nullptr) {
  //   if (!elem->Finished(now)) {
  //     prev = elem;
  //     elem = elem->next_;
  //     continue;
  //   }
  //   g_timestamps_callback(elem->arg_, &(elem->ts_),
  //                         absl::DeadlineExceededError("Ack timed out"));
  //   if (prev != nullptr) {
  //     prev->next_ = elem->next_;
  //     delete elem;
  //     elem = prev->next_;
  //   } else {
  //     head_ = elem->next_;
  //     delete elem;
  //     elem = head_;
  //   }
  // }
  // tail_ = (head_ == nullptr) ? head_ : prev;
}

void TracedBufferList::Shutdown(
    std::optional<EventEngine::Endpoint::WriteEventSink> remaining,
    absl::Status shutdown_err) {
  // grpc_core::MutexLock lock(&mu_);
  // while (head_) {
  //   TracedBuffer* elem = head_;
  //   g_timestamps_callback(elem->arg_, &(elem->ts_), shutdown_err);
  //   head_ = head_->next_;
  //   delete elem;
  // }
  // if (remaining.has_value()) {
  //   remaining->TakeEventCallback()()
  //       g_timestamps_callback(remaining, nullptr, shutdown_err);
  // }
  // tail_ = head_;
}

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_LINUX_ERRQUEUE
