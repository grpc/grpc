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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TRACED_BUFFER_LIST_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TRACED_BUFFER_LIST_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stdint.h>

#include <optional>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "src/core/lib/event_engine/posix_engine/internal_errqueue.h"
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/event_engine/posix_engine/posix_write_event_sink.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

// TracedBuffer is a class to keep track of timestamps for a specific buffer in
// the TCP layer. We are only tracking timestamps for Linux kernels and hence
// this class would only be used by Linux platforms. For all other platforms,
// TracedBuffer would be an empty class.
// The timestamps collected are according to Timestamps declared above A
// TracedBuffer list is kept track of using the head element of the list. If
// *the head element of the list is nullptr, then the list is empty.
#ifdef GRPC_LINUX_ERRQUEUE

class TracedBufferList {
 public:
  TracedBufferList() = default;
  ~TracedBufferList() { Shutdown(std::nullopt); }

  // Add a new entry in the TracedBuffer list pointed to by head. Also saves
  // sendmsg_time with the current timestamp.
  void AddNewEntry(int32_t seq_no, EventEnginePosixInterface* posix_interface,
                   const FileDescriptor& fd,
                   EventEngine::Endpoint::WriteEventSink sink);
  // Processes a received timestamp based on sock_extended_err and
  // scm_timestamping structures. It will invoke the timestamps callback if the
  // timestamp type is SCM_TSTAMP_ACK.
  void ProcessTimestamp(struct sock_extended_err* serr,
                        struct cmsghdr* opt_stats,
                        struct scm_timestamping* tss);
  // The Size() operation is slow and is used only in tests.
  int Size() {
    grpc_core::MutexLock lock(&mu_);
    return list_.size();
  }
  // Cleans the list by calling the callback for each traced buffer in the list
  // with timestamps that it has.
  void Shutdown(std::optional<EventEngine::Endpoint::WriteEventSink> remaining);

  // Sets the maximum time we wait for a traced buffer to be Acked. Counted from
  // the previous received event for the traced buffer.
  static void TestOnlySetMaxPendingAckTime(grpc_core::Duration duration);

 private:
  class Metrics {};

  class TracedBuffer {
   public:
    TracedBuffer(uint32_t seq_no, EventEngine::Endpoint::WriteEventSink sink)
        : seq_no_(seq_no), sink_(std::move(sink)) {}
    // Returns true if the TracedBuffer is considered stale at the given
    // timestamp.
    bool TimedOut(grpc_core::Timestamp now);

   private:
    friend class TracedBufferList;
    grpc_core::Timestamp last_timestamp_;
    uint32_t seq_no_;  // The sequence number for the last byte in the buffer
    PosixWriteEventSink sink_;
  };
  grpc_core::Mutex mu_;
  // TracedBuffers are ordered by sequence number and would need to be processed
  // in a FIFO order starting with the smallest sequence number.
  std::list<TracedBuffer> list_ ABSL_GUARDED_BY(mu_);
};

#else   // GRPC_LINUX_ERRQUEUE
// TracedBufferList implementation is a no-op for this platform.
class TracedBufferList {
 public:
  void AddNewEntry(int32_t /*seq_no*/, int /*fd*/, void* /*arg*/) {}
  void ProcessTimestamp(struct sock_extended_err* /*serr*/,
                        struct cmsghdr* /*opt_stats*/,
                        struct scm_timestamping* /*tss*/) {}
  int Size() { return 0; }
  void Shutdown(
      std::optional<EventEngine::Endpoint::WriteEventSink> /*remaining*/) {}
  static void TestOnlySetMaxPendingAckTime(grpc_core::Duration /*duration*/);
};
#endif  // GRPC_LINUX_ERRQUEUE

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TRACED_BUFFER_LIST_H
