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
#include <time.h>

#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {
void TracedBuffer::AddNewEntry(TracedBuffer** head, uint32_t seq_no,
                               void* arg) {
  GPR_DEBUG_ASSERT(head != nullptr);
  TracedBuffer* new_elem = New<TracedBuffer>(seq_no, arg);
  /* Store the current time as the sendmsg time. */
  new_elem->ts_.sendmsg_time = gpr_now(GPR_CLOCK_REALTIME);
  new_elem->ts_.scheduled_time = gpr_inf_past(GPR_CLOCK_REALTIME);
  new_elem->ts_.sent_time = gpr_inf_past(GPR_CLOCK_REALTIME);
  new_elem->ts_.acked_time = gpr_inf_past(GPR_CLOCK_REALTIME);
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
} /* namespace */

void TracedBuffer::ProcessTimestamp(TracedBuffer** head,
                                    struct sock_extended_err* serr,
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
          fill_gpr_from_timestamp(&(elem->ts_.scheduled_time), &(tss->ts[0]));
          elem = elem->next_;
          break;
        case SCM_TSTAMP_SND:
          fill_gpr_from_timestamp(&(elem->ts_.sent_time), &(tss->ts[0]));
          elem = elem->next_;
          break;
        case SCM_TSTAMP_ACK:
          fill_gpr_from_timestamp(&(elem->ts_.acked_time), &(tss->ts[0]));
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
