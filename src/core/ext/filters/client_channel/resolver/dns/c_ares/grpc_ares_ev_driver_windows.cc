/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"
#if GRPC_ARES == 1 && defined(GPR_WINDOWS)

#include <ares.h>
#include <string.h>
#include "src/core/lib/gprpp/memory.h"

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

namespace grpc_core {

/* TODO: fill in the body of GrpcPolledFdWindows to enable c-ares on Windows.
   This dummy implementation only allows grpc to compile on windows with
   GRPC_ARES=1. */
class GrpcPolledFdWindows : public GrpcPolledFd {
 public:
  GrpcPolledFdWindows() { abort(); }
  ~GrpcPolledFdWindows() { abort(); }
  void RegisterForOnReadableLocked(grpc_closure* read_closure) override {
    abort();
  }
  void RegisterForOnWriteableLocked(grpc_closure* write_closure) override {
    abort();
  }
  bool IsFdStillReadableLocked() override { abort(); }
  void ShutdownLocked(grpc_error* error) override { abort(); }
  ares_socket_t GetWrappedAresSocketLocked() override { abort(); }
  const char* GetName() override { abort(); }
};

GrpcPolledFd* NewGrpcPolledFdLocked(ares_socket_t as,
                                    grpc_pollset_set* driver_pollset_set) {
  return nullptr;
}

void ConfigureAresChannelLocked(ares_channel* channel) { abort(); }

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GPR_WINDOWS) */
