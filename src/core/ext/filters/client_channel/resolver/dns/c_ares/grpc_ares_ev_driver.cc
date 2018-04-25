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
#if GRPC_ARES == 1 && !defined(GRPC_UV)

#include <ares.h>
#include <memory.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

namespace grpc_core {

FdNode::FdNode() : InternallyRefCounted() {
  gpr_mu_init(&mu_);
  readable_registered_ = false;
  writable_registered_ = false;
  shutting_down_ = false;
}

FdNode::~FdNode() {
  GPR_ASSERT(!readable_registered_);
  GPR_ASSERT(!writable_registered_);
  GPR_ASSERT(shutting_down_);
  gpr_mu_destroy(&mu_);
}

struct FdNodeEventArg {
  FdNodeEventArg(RefCountedPtr<FdNode> fdn,
                 RefCountedPtr<AresEvDriver> ev_driver)
      : fdn(fdn), ev_driver(ev_driver){};
  RefCountedPtr<FdNode> fdn;
  RefCountedPtr<AresEvDriver> ev_driver;
};

void FdNode::MaybeRegisterForReadsAndWrites(
    RefCountedPtr<AresEvDriver> ev_driver, int socks_bitmask, size_t idx) {
  gpr_mu_lock(&mu_);
  // Register read_closure if the socket is readable and read_closure has
  // not been registered with this socket.
  if (ARES_GETSOCK_READABLE(socks_bitmask, idx) && !readable_registered_) {
    GRPC_CLOSURE_INIT(&read_closure_, &FdNode::OnReadable,
                      grpc_core::New<FdNodeEventArg>(Ref(), ev_driver),
                      grpc_schedule_on_exec_ctx);
    RegisterForOnReadable();
    readable_registered_ = true;
  }
  // Register write_closure if the socket is writable and write_closure
  // has not been registered with this socket.
  if (ARES_GETSOCK_WRITABLE(socks_bitmask, idx) && !writable_registered_) {
    GRPC_CLOSURE_INIT(&write_closure_, &FdNode::OnWriteable,
                      grpc_core::New<FdNodeEventArg>(Ref(), ev_driver),
                      grpc_schedule_on_exec_ctx);
    RegisterForOnWriteable();
    writable_registered_ = true;
  }
  gpr_mu_unlock(&mu_);
}

void FdNode::Shutdown() {
  gpr_mu_lock(&mu_);
  shutting_down_ = true;
  gpr_mu_unlock(&mu_);
  ShutdownInnerEndpoint();
}

void FdNode::OnReadable(void* arg, grpc_error* error) {
  UniquePtr<FdNodeEventArg> event_arg(reinterpret_cast<FdNodeEventArg*>(arg));
  event_arg->fdn->OnReadableInner(event_arg->ev_driver.get(), error);
}

void FdNode::OnWriteable(void* arg, grpc_error* error) {
  UniquePtr<FdNodeEventArg> event_arg(reinterpret_cast<FdNodeEventArg*>(arg));
  event_arg->fdn->OnWriteableInner(event_arg->ev_driver.get(), error);
}

void FdNode::OnReadableInner(AresEvDriver* ev_driver, grpc_error* error) {
  gpr_mu_lock(&mu_);
  readable_registered_ = false;
  if (shutting_down_ && !writable_registered_) {
    gpr_mu_unlock(&mu_);
    return;
  }
  gpr_mu_unlock(&mu_);

  gpr_log(GPR_DEBUG, "readable on %" PRIdPTR, (uintptr_t)GetInnerEndpoint());
  if (error == GRPC_ERROR_NONE) {
    do {
      ares_process_fd(ev_driver->GetChannel(), GetInnerEndpoint(),
                      ARES_SOCKET_BAD);
    } while (ShouldRepeatReadForAresProcessFd());
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this ev_driver will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be
    // invoked with a status of ARES_ECANCELLED. The remaining file
    // descriptors in this ev_driver will be cleaned up in the follwing
    // ev_driver->NotifyOnEvent().
    ares_cancel(ev_driver->GetChannel());
  }
  ev_driver->NotifyOnEvent();
}

void FdNode::OnWriteableInner(AresEvDriver* ev_driver, grpc_error* error) {
  gpr_mu_lock(&mu_);
  writable_registered_ = false;
  if (shutting_down_ && !readable_registered_) {
    gpr_mu_unlock(&mu_);
    return;
  }
  gpr_mu_unlock(&mu_);

  gpr_log(GPR_DEBUG, "writable on %" PRIdPTR, (uintptr_t)GetInnerEndpoint());
  if (error == GRPC_ERROR_NONE) {
    ares_process_fd(ev_driver->GetChannel(), ARES_SOCKET_BAD,
                    GetInnerEndpoint());
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this ev_driver will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be
    // invoked with a status of ARES_ECANCELLED. The remaining file
    // descriptors in this ev_driver will be cleaned up in the follwing
    // grpc_ares_notify_on_event_locked().
    ares_cancel(ev_driver->GetChannel());
  }
  ev_driver->NotifyOnEvent();
}

AresEvDriver::AresEvDriver() : InternallyRefCounted() {
  gpr_mu_init(&mu_);
  fds_ = UniquePtr<InlinedVector<RefCountedPtr<FdNode>, ARES_GETSOCK_MAXNUM>>(
      grpc_core::New<
          InlinedVector<RefCountedPtr<FdNode>, ARES_GETSOCK_MAXNUM>>());
  working_ = false;
  shutting_down_ = false;
}

AresEvDriver::~AresEvDriver() {
  GPR_ASSERT(fds_->size() == 0);
  gpr_mu_destroy(&mu_);
  ares_destroy(channel_);
}

void AresEvDriver::Start() {
  gpr_mu_lock(&mu_);
  if (!working_) {
    working_ = true;
    NotifyOnEventLocked();
  }
  gpr_mu_unlock(&mu_);
}

void AresEvDriver::Destroy() {
  // We mark the event driver
  // as being shut down. If the event driver is working,
  // grpc_ares_notify_on_event_locked will shut down the fds; if it's not
  // working, there are no fds to shut down.
  gpr_mu_lock(&mu_);
  shutting_down_ = true;
  gpr_mu_unlock(&mu_);
  Unref();
}

void AresEvDriver::Shutdown() {
  gpr_mu_lock(&mu_);
  shutting_down_ = true;
  for (size_t i = 0; i < fds_->size(); i++) {
    (*fds_)[i]->ShutdownInnerEndpoint();
  }
  gpr_mu_unlock(&mu_);
}

ares_channel AresEvDriver::GetChannel() { return channel_; }
ares_channel* AresEvDriver::GetChannelPointer() { return &channel_; }

void AresEvDriver::NotifyOnEvent() {
  gpr_mu_lock(&mu_);
  NotifyOnEventLocked();
  gpr_mu_unlock(&mu_);
}

void AresEvDriver::NotifyOnEventLocked() {
  UniquePtr<InlinedVector<RefCountedPtr<FdNode>, ARES_GETSOCK_MAXNUM>> new_list(
      grpc_core::New<
          InlinedVector<RefCountedPtr<FdNode>, ARES_GETSOCK_MAXNUM>>());
  if (!shutting_down_) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int socks_bitmask = ares_getsock(channel_, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        int existing_index = LookupFdNodeIndex(socks[i]);
        // Create a new fd_node if sock[i] is not in the fd_node list.
        if (existing_index == -1) {
          gpr_log(GPR_DEBUG, "new fd: %d", socks[i]);
          char* fd_name;
          gpr_asprintf(&fd_name, "ares_ev_driver-%" PRIuPTR, i);
          auto fdn = RefCountedPtr<FdNode>(CreateFdNode(socks[i], fd_name));
          gpr_free(fd_name);
          new_list->push_back(fdn);
        } else {
          new_list->push_back((*fds_)[existing_index]);
          (*fds_)[existing_index] = nullptr;
        }
        (*new_list)[new_list->size() - 1]->MaybeRegisterForReadsAndWrites(
            Ref(), socks_bitmask, i);
      }
    }
  }
  // Any remaining fds in ev_driver->fds were not returned by ares_getsock()
  // and are therefore no longer in use, so they can be shut down and removed
  // from the list.
  for (size_t i = 0; i < fds_->size(); i++) {
    if ((*fds_)[i] != nullptr) {
      (*fds_)[i]->Shutdown();
    }
  }
  // If the ev driver has no working fd, all the tasks are done.
  if (fds_->size() == 0) {
    working_ = false;
    gpr_log(GPR_DEBUG, "ev driver stop working");
  }
  fds_ = std::move(new_list);
}

int AresEvDriver::LookupFdNodeIndex(ares_socket_t as) {
  for (size_t i = 0; i < fds_->size(); i++) {
    if ((*fds_)[i] != nullptr && (*fds_)[i]->GetInnerEndpoint() == as) {
      return i;
    }
  }
  return -1;
}

grpc_error* AresEvDriver::CreateAndInitialize(AresEvDriver** ev_driver,
                                              grpc_pollset_set* pollset_set) {
  *ev_driver = AresEvDriver::Create(pollset_set);
  int status = ares_init(&(*ev_driver)->channel_);
  gpr_log(GPR_DEBUG, "grpc_ares_ev_driver_create:%" PRIdPTR,
          (uintptr_t)*ev_driver);
  if (status != ARES_SUCCESS) {
    char* err_msg;
    gpr_asprintf(&err_msg, "Failed to init ares channel. C-ares error: %s",
                 ares_strerror(status));
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_msg);
    gpr_free(err_msg);
    Delete(*ev_driver);
    *ev_driver = nullptr;
    return err;
  }
  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && !defined(GRPC_UV) */
