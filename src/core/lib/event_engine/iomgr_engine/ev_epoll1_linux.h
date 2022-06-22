/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_EV_EPOLL1_LINUX_H
#define GRPC_CORE_LIB_IOMGR_EV_EPOLL1_LINUX_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/iomgr_engine/ev_posix.h"

namespace grpc_event_engine {
namespace iomgr_engine {

class Epoll1Poller : public EventPoller {
 public:
  EventHandle* FdCreate(int fd, absl::string_view name,
                        bool track_err) override;
  int FdWrappedFd(EventHandle* fd) override;
  void FdOrphan(EventHandle* fd, IomgrEngineClosure* on_done, int* release_fd,
                absl::string_view reason) override;
  void FdShutdown(EventHandle* fd, absl::Status why) override;
  void FdNotifyOnRead(EventHandle* fd, IomgrEngineClosure* on_read) override;
  void FdNotifyOnWrite(EventHandle* fd, IomgrEngineClosure* on_write) override;
  void FdNotifyOnError(EventHandle* fd, IomgrEngineClosure* on_error) override;
  void FdSetReadable(EventHandle* fd) override;
  void FdSetWritable(EventHandle* fd) override;
  void FdSetError(EventHandle* fd) override;
  bool FdIsShutdown(EventHandle* fd) override;
  virtual absl::StatusOr<std::vector<experimental::EventEngine::Closure*>> Work(
      grpc_core::Timestamp deadline) = 0;
};
}  // namespace iomgr_engine
}  // namespace grpc_event_engine

// a polling engine that utilizes a singleton epoll set and turnstile polling

const grpc_event_engine_vtable* grpc_init_epoll1_linux(bool explicit_request);

#endif /* GRPC_CORE_LIB_IOMGR_EV_EPOLL1_LINUX_H */
