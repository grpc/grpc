/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/port.h"

#include <grpc/support/log.h>

#ifdef GRPC_LINUX_ERRQUEUE
#include <string.h>
#include <time.h>

#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {
void TracedBuffer::AddNewEntry(TracedBuffer** head, uint32_t seq_no,
                               void* arg) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* new_elem = New<TracedBuffer>(seq_no, arg);
  /* Store the current time as the sendmsg time. */
  new_elem->ts_.sendmsg_time.time = gpr_now(GPR_CLOCK_REALTIME);
  new_elem->ts_.scheduled_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  new_elem->ts_.sent_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  new_elem->ts_.acked_time.time = gpr_inf_past(GPR_CLOCK_REALTIME);
  if (*head == nullptr) {
    *head = new_elem;
    return;
  }
  /* Append at the end. */
  TracedBuffer* ptr = *head;
  while (ptr->next_ != nullptr) {
    ptr = ptr->next_;
  }
  ptr->next_ = new_elem;
}

namespace {
/** Fills gpr_timespec gts based on values from timespec ts */
void fill_gpr_from_timestamp(gpr_timespec* gts, const struct timespec* ts) {
  gts->tv_sec = ts->tv_sec;
  gts->tv_nsec = static_cast<int32_t>(ts->tv_nsec);
  gts->clock_type = GPR_CLOCK_REALTIME;
}

void default_timestamps_callback(void* arg, grpc_core::Timestamps* ts,
                                 grpc_error* shudown_err) {
  gpr_log(GPR_DEBUG, "Timestamps callback has not been registered");
}

/** The saved callback function that will be invoked when we get all the
 * timestamps that we are going to get for a TracedBuffer. */
void (*timestamps_callback)(void*, grpc_core::Timestamps*,
                            grpc_error* shutdown_err) =
    default_timestamps_callback;

/* Used to extract individual opt stats from cmsg, so as to avoid troubles with
 * unaligned reads */
template <typename T>
T read_unaligned(const void* ptr) {
  T val;
  memcpy(&val, ptr, sizeof(val));
  return val;
}

/** Adds opt stats statistics from the given control message to the connection
 * metrics. */
void ExtractOptStats(ConnectionMetrics* conn_metrics,
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
        conn_metrics->busy_usec.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_RWND_LIMITED: {
        conn_metrics->rwnd_limited_usec.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_SNDBUF_LIMITED: {
        conn_metrics->sndbuf_limited_usec.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_PACING_RATE: {
        conn_metrics->pacing_rate.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DELIVERY_RATE: {
        conn_metrics->delivery_rate.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DELIVERY_RATE_APP_LMT: {
        conn_metrics->is_delivery_rate_app_limited =
            read_unaligned<uint8_t>(val);
        break;
      }
      case TCP_NLA_SND_CWND: {
        conn_metrics->congestion_window.set(read_unaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_MIN_RTT: {
        conn_metrics->min_rtt.set(read_unaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_SRTT: {
        conn_metrics->srtt.set(read_unaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_RECUR_RETRANS: {
        conn_metrics->recurring_retrans.set(read_unaligned<uint8_t>(val));
        break;
      }
      case TCP_NLA_BYTES_SENT: {
        conn_metrics->data_sent.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DATA_SEGS_OUT: {
        conn_metrics->packet_sent.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_TOTAL_RETRANS: {
        conn_metrics->packet_retx.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_DELIVERED: {
        conn_metrics->packet_delivered.set(read_unaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_DELIVERED_CE: {
        conn_metrics->packet_delivered_ce.set(read_unaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_BYTES_RETRANS: {
        conn_metrics->data_retx.set(read_unaligned<uint64_t>(val));
        break;
      }
      case TCP_NLA_REORDERING: {
        conn_metrics->reordering.set(read_unaligned<uint32_t>(val));
        break;
      }
      case TCP_NLA_SND_SSTHRESH: {
        conn_metrics->snd_ssthresh.set(read_unaligned<uint32_t>(val));
        break;
      }
    }
    offset += NLA_ALIGN(attr->nla_len);
  }
}
} /* namespace */

void TracedBuffer::ProcessTimestamp(TracedBuffer** head,
                                    struct sock_extended_err* serr,
                                    struct cmsghdr* opt_stats,
                                    struct scm_timestamping* tss) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* elem = *head;
  TracedBuffer* next = nullptr;
  while (elem != nullptr) {
    /* The byte number refers to the sequence number of the last byte which this
     * timestamp relates to. */
    if (serr->ee_data >= elem->seq_no_) {
      switch (serr->ee_info) {
        case SCM_TSTAMP_SCHED:
          fill_gpr_from_timestamp(&(elem->ts_.scheduled_time.time),
                                  &(tss->ts[0]));
          ExtractOptStats(&(elem->ts_.scheduled_time.metrics), opt_stats);
          elem = elem->next_;
          break;
        case SCM_TSTAMP_SND:
          fill_gpr_from_timestamp(&(elem->ts_.sent_time.time), &(tss->ts[0]));
          ExtractOptStats(&(elem->ts_.sent_time.metrics), opt_stats);
          elem = elem->next_;
          break;
        case SCM_TSTAMP_ACK:
          fill_gpr_from_timestamp(&(elem->ts_.acked_time.time), &(tss->ts[0]));
          ExtractOptStats(&(elem->ts_.acked_time.metrics), opt_stats);
          /* Got all timestamps. Do the callback and free this TracedBuffer.
           * The thing below can be passed by value if we don't want the
           * restriction on the lifetime. */
          timestamps_callback(elem->arg_, &(elem->ts_), GRPC_ERROR_NONE);
          next = elem->next_;
          Delete<TracedBuffer>(elem);
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
                            grpc_error* shutdown_err) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* elem = *head;
  while (elem != nullptr) {
    timestamps_callback(elem->arg_, &(elem->ts_), shutdown_err);
    auto* next = elem->next_;
    Delete<TracedBuffer>(elem);
    elem = next;
  }
  *head = nullptr;
  if (remaining != nullptr) {
    timestamps_callback(remaining, nullptr, shutdown_err);
  }
  GRPC_ERROR_UNREF(shutdown_err);
}

void grpc_tcp_set_write_timestamps_callback(void (*fn)(void*,
                                                       grpc_core::Timestamps*,
                                                       grpc_error* error)) {
  timestamps_callback = fn;
}
} /* namespace grpc_core */

#else /* GRPC_LINUX_ERRQUEUE */

namespace grpc_core {
void grpc_tcp_set_write_timestamps_callback(void (*fn)(void*,
                                                       grpc_core::Timestamps*,
                                                       grpc_error* error)) {
  gpr_log(GPR_DEBUG, "Timestamps callback is not enabled for this platform");
}
} /* namespace grpc_core */

#endif /* GRPC_LINUX_ERRQUEUE */
