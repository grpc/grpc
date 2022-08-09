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

#include "src/core/lib/event_engine/posix_engine/traced_buffer_list.h"

#include <linux/errqueue.h>
#include <linux/netlink.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <cstdint>
#include <utility>

#include "absl/functional/any_invocable.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_LINUX_ERRQUEUE
#include <netinet/in.h>
#include <string.h>
#include <time.h>

namespace grpc_event_engine {
namespace posix_engine {

namespace {
// Fills gpr_timespec gts based on values from timespec ts.
void FillGprFromTimestamp(gpr_timespec* gts, const struct timespec* ts) {
  gts->tv_sec = ts->tv_sec;
  gts->tv_nsec = static_cast<int32_t>(ts->tv_nsec);
  gts->clock_type = GPR_CLOCK_REALTIME;
}

void DefaultTimestampsCallback(void* /*arg*/, Timestamps* /*ts*/,
                               absl::Status /*shudown_err*/) {
  gpr_log(GPR_DEBUG, "Timestamps callback has not been registered");
}

// The saved callback function that will be invoked when we get all the
// timestamps that we are going to get for a TracedBuffer.
absl::AnyInvocable<void(void*, Timestamps*, absl::Status)>
    g_timestamps_callback =
        []() -> absl::AnyInvocable<void(void*, Timestamps*, absl::Status)> {
  return DefaultTimestampsCallback;
}();

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
    metrics->recurring_retrans.emplace(info->tcpi_retransmits);
    metrics->is_delivery_rate_app_limited.emplace(
        info->tcpi_delivery_rate_app_limited);
    metrics->congestion_window.emplace(info->tcpi_snd_cwnd);
    metrics->reordering.emplace(info->tcpi_reordering);
    metrics->packet_retx.emplace(info->tcpi_total_retrans);
    metrics->pacing_rate.emplace(info->tcpi_pacing_rate);
    metrics->data_notsent.emplace(info->tcpi_notsent_bytes);
    if (info->tcpi_min_rtt != UINT32_MAX) {
      metrics->min_rtt.emplace(info->tcpi_min_rtt);
    }
    metrics->packet_sent.emplace(info->tcpi_data_segs_out);
    metrics->delivery_rate.emplace(info->tcpi_delivery_rate);
    metrics->busy_usec.emplace(info->tcpi_busy_time);
    metrics->rwnd_limited_usec.emplace(info->tcpi_rwnd_limited);
    metrics->sndbuf_limited_usec.emplace(info->tcpi_sndbuf_limited);
  }
  if (info->length > offsetof(tcp_info, tcpi_dsack_dups)) {
    metrics->data_sent.emplace(info->tcpi_bytes_sent);
    metrics->data_retx.emplace(info->tcpi_bytes_retrans);
    metrics->packet_spurious_retx.emplace(info->tcpi_dsack_dups);
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
        metrics->busy_usec.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_RWND_LIMITED: {
        metrics->rwnd_limited_usec.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_SNDBUF_LIMITED: {
        metrics->sndbuf_limited_usec.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_PACING_RATE: {
        metrics->pacing_rate.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DELIVERY_RATE: {
        metrics->delivery_rate.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DELIVERY_RATE_APP_LMT: {
        metrics->is_delivery_rate_app_limited.emplace(
            ReadUnaligned<uint8_t>(val));
        break;
      }
      case TCP_NLA_SND_CWND: {
        metrics->congestion_window.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_MIN_RTT: {
        metrics->min_rtt.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_SRTT: {
        metrics->srtt.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_RECUR_RETRANS: {
        metrics->recurring_retrans.emplace(ReadUnaligned<uint8_t>(val));
        break;
      }
      case TCP_NLA_BYTES_SENT: {
        metrics->data_sent.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DATA_SEGS_OUT: {
        metrics->packet_sent.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_TOTAL_RETRANS: {
        metrics->packet_retx.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DELIVERED: {
        metrics->packet_delivered.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_DELIVERED_CE: {
        metrics->packet_delivered_ce.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_BYTES_RETRANS: {
        metrics->data_retx.emplace(ReadUnaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DSACK_DUPS: {
        metrics->packet_spurious_retx.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_REORDERING: {
        metrics->reordering.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_SND_SSTHRESH: {
        metrics->snd_ssthresh.emplace(ReadUnaligned<uint32_t>(val));
        break;
      }
    }
    offset += NLA_ALIGN(attr->nla_len);
  }
}

int GetSocketTcpInfo(tcp_info* info, int fd) {
  memset(info, 0, sizeof(*info));
  info->length = offsetof(tcp_info, length);
  return getsockopt(fd, IPPROTO_TCP, TCP_INFO, info, &(info->length));
}
}  // namespace.

void TracedBuffer::AddNewEntry(TracedBuffer** head, uint32_t seq_no, int fd,
                               void* arg) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* new_elem = new TracedBuffer(seq_no, arg);
  // Store the current time as the sendmsg time.
  new_elem->ts_.sendmsg_time.time = gpr_now(GPR_CLOCK_REALTIME);
  new_elem->ts_.scheduled_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  new_elem->ts_.sent_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  new_elem->ts_.acked_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);

  if (GetSocketTcpInfo(&new_elem->ts_.info, fd) == 0) {
    ExtractOptStatsFromTcpInfo(&new_elem->ts_.sendmsg_time.metrics,
                               &new_elem->ts_.info);
  }
  if (*head == nullptr) {
    *head = new_elem;
    return;
  }
  // Append at the end.
  TracedBuffer* ptr = *head;
  while (ptr->next_ != nullptr) {
    ptr = ptr->next_;
  }
  ptr->next_ = new_elem;
}

void TracedBuffer::ProcessTimestamp(TracedBuffer** head,
                                    struct sock_extended_err* serr,
                                    struct cmsghdr* opt_stats,
                                    struct scm_timestamping* tss) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* elem = *head;
  TracedBuffer* next = nullptr;
  while (elem != nullptr) {
    // The byte number refers to the sequence number of the last byte which this
    // timestamp relates to.
    if (serr->ee_data >= elem->seq_no_) {
      switch (serr->ee_info) {
        case SCM_TSTAMP_SCHED:
          FillGprFromTimestamp(&(elem->ts_.scheduled_time.time), &(tss->ts[0]));
          ExtractOptStatsFromCmsg(&(elem->ts_.scheduled_time.metrics),
                                  opt_stats);
          elem = elem->next_;
          break;
        case SCM_TSTAMP_SND:
          FillGprFromTimestamp(&(elem->ts_.sent_time.time), &(tss->ts[0]));
          ExtractOptStatsFromCmsg(&(elem->ts_.sent_time.metrics), opt_stats);
          elem = elem->next_;
          break;
        case SCM_TSTAMP_ACK:
          FillGprFromTimestamp(&(elem->ts_.acked_time.time), &(tss->ts[0]));
          ExtractOptStatsFromCmsg(&(elem->ts_.acked_time.metrics), opt_stats);
          // Got all timestamps. Do the callback and free this TracedBuffer. The
          // thing below can be passed by value if we don't want the restriction
          // on the lifetime.
          g_timestamps_callback(elem->arg_, &(elem->ts_), absl::OkStatus());
          next = elem->next_;
          delete static_cast<TracedBuffer*>(elem);
          *head = elem = next;
          break;
        default:
          abort();
      }
    } else {
      break;
    }
  }
}

void TracedBuffer::Shutdown(TracedBuffer** head, void* remaining,
                            absl::Status shutdown_err) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* elem = *head;
  while (elem != nullptr) {
    g_timestamps_callback(elem->arg_, &(elem->ts_), shutdown_err);
    auto* next = elem->next_;
    delete elem;
    elem = next;
  }
  *head = nullptr;
  if (remaining != nullptr) {
    g_timestamps_callback(remaining, nullptr, shutdown_err);
  }
}

void TcpSetWriteTimestampsCallback(
    absl::AnyInvocable<void(void*, Timestamps*, absl::Status)> fn) {
  g_timestamps_callback = std::move(fn);
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

#else /* GRPC_LINUX_ERRQUEUE */

namespace grpc_event_engine {
namespace posix_engine {

void TcpSetWriteTimestampsCallback(
    absl::AnyInvocable<void(void*, Timestamps*, absl::Status)> /*fn*/) {
  GPR_ASSERT(false && "Timestamps callback is not enabled for this platform");
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif /* GRPC_LINUX_ERRQUEUE */
