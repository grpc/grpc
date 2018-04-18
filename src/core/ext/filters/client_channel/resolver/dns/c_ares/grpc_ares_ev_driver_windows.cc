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

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"

namespace grpc_core {

class AresEvDriverWindows;

class FdNodeWindows final : public FdNode {
 public:
  FdNodeWindows(grpc_winsocket* winsocket) : FdNode() {
    winsocket_ = winsocket;
  }
  ~FdNodeWindows() {
    gpr_log(GPR_DEBUG, "delete socket: %" PRIdPTR,
            (uintptr_t)GetInnerEndpoint());
    grpc_winsocket_destroy(winsocket_);
  }

  void ShutdownInnerEndpoint() override { grpc_winsocket_shutdown(winsocket_); }

  ares_socket_t GetInnerEndpoint() override {
    return grpc_winsocket_wrapped_socket(winsocket_);
  }

  bool ShouldRepeatReadForAresProcessFd() override {
    // On windows, we are sure to get another chance
    // at ares_process_fd for anything that
    // ARES_GETSOCK_READABLE returns, because we are
    // busylooping with GRPC_CLOSURE_SCHED.
    return false;
  }

  void RegisterForOnReadable() override {
    // From original PR #12416:
    // There's not a lot of good ways to poll sockets using the IOCP loop.
    // We could start a separate thread to start select()ing on these, and
    // kick the main IOCP when we get a result, but this is a bit of
    // synchronization nightmare, as we'd also need to be able to kick,
    // pause and restart that thread. We could also poke at the MSAFD dll
    // directly, the same way this code does:
    //  https://github.com/piscisaureus/epoll_windows/blob/master/src/afd.h
    // but this is a lot of black magic and a lot of work that I'm not sure
    // I want to maintain. So right now, in order to get something working
    // that I can revisit later, I'm simply going to busy-wait the reads
    // and writes. Since the license epoll_windows seems to be BSD, we
    // could drop its afd code in there. Or maybe I'll add a thread if push
    // comes to shove.
    GRPC_CLOSURE_SCHED(&read_closure_, GRPC_ERROR_NONE);
  }
  void RegisterForOnWriteable() override {
    GRPC_CLOSURE_SCHED(&write_closure_, GRPC_ERROR_NONE);
  }

 private:
  grpc_winsocket* winsocket_;
};

class AresEvDriverWindows final : public AresEvDriver {
 public:
  AresEvDriverWindows() : AresEvDriver() {}

 private:
  FdNode* CreateFdNode(ares_socket_t as, const char* name) override {
    grpc_winsocket* winsocket = grpc_winsocket_create(as, name);
    return grpc_core::New<FdNodeWindows>(winsocket);
  }
};

AresEvDriver* AresEvDriver::Create(grpc_pollset_set* pollset_set) {
  (void*)pollset_set;
  return grpc_core::New<AresEvDriverWindows>();
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GPR_WINDOWS) */
