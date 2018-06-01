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

#ifndef GRPC_CORE_LIB_IOMGR_BUFFER_LIST_H
#define GRPC_CORE_LIB_IOMGR_BUFFER_LIST_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include <grpc/support/time.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/internal_errqueue.h"

namespace grpc_core {
struct Timestamps {
  gpr_timespec sendmsg_time;
  gpr_timespec scheduled_time;
  gpr_timespec sent_time;
  gpr_timespec acked_time;
};

#ifdef GRPC_LINUX_ERRQUEUE
class TracedBuffer {
 public:
  /** Add a new entry in the TracedBuffer list pointed to by head */
  static void AddNewEntry(grpc_core::TracedBuffer** head, uint32_t seq_no,
                          void* arg);

  /** Processes a timestamp received */
  static void ProcessTimestamp(grpc_core::TracedBuffer** head,
                               struct sock_extended_err* serr,
                               struct scm_timestamping* tss);

  /** Calls the callback for each traced buffer in the list with timestamps that
   * it has. */
  static void Shutdown(grpc_core::TracedBuffer** head,
                       grpc_error* shutdown_err);

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW

  TracedBuffer(int seq_no, void* arg)
      : seq_no_(seq_no), arg_(arg), next_(nullptr) {
    gpr_log(GPR_INFO, "seq_no %d", seq_no_);
  }

  uint32_t seq_no_; /* The sequence number for the last byte in the buffer */
  void* arg_;       /* The arg to pass to timestamps_callback */
  grpc_core::Timestamps ts_;
  grpc_core::TracedBuffer* next_;
};
#else  /* GRPC_LINUX_ERRQUEUE */
class TracedBuffer {};
#endif /* GRPC_LINUX_ERRQUEUE */

/** Sets the timestamp callback */
void grpc_tcp_set_write_timestamps_callback(void (*fn)(void*,
                                                       grpc_core::Timestamps*,
                                                       grpc_error* error));

};  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_BUFFER_LIST_H */
