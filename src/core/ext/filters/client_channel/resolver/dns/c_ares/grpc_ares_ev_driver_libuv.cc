/*
 *
 * Copyright 2019 gRPC authors.
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
#if GRPC_ARES == 1 && defined(GRPC_UV)

#include <ares.h>
#include <uv.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/combiner.h"

namespace grpc_core {

void ares_uv_poll_cb(uv_poll_t* handle, int status, int events);

void ares_uv_poll_close_cb(uv_handle_t* handle) { Delete(handle); }

class GrpcPolledFdLibuv : public GrpcPolledFd {
 public:
  GrpcPolledFdLibuv(ares_socket_t as, grpc_combiner* combiner)
      : as_(as), combiner_(combiner) {
    gpr_asprintf(&name_, "c-ares socket: %" PRIdPTR, (intptr_t)as);
    handle_ = New<uv_poll_t>();
    uv_poll_init_socket(uv_default_loop(), handle_, as);
    handle_->data = this;
    GRPC_COMBINER_REF(combiner_, "libuv ares event driver");
  }

  ~GrpcPolledFdLibuv() {
    gpr_free(name_);
    GRPC_COMBINER_UNREF(combiner_, "libuv ares event driver");
  }

  void RegisterForOnReadableLocked(grpc_closure* read_closure) override {
    GPR_ASSERT(read_closure_ == nullptr);
    GPR_ASSERT((poll_events_ & UV_READABLE) == 0);
    read_closure_ = read_closure;
    poll_events_ |= UV_READABLE;
    uv_poll_start(handle_, poll_events_, ares_uv_poll_cb);
  }

  void RegisterForOnWriteableLocked(grpc_closure* write_closure) override {
    GPR_ASSERT(write_closure_ == nullptr);
    GPR_ASSERT((poll_events_ & UV_WRITABLE) == 0);
    write_closure_ = write_closure;
    poll_events_ |= UV_WRITABLE;
    uv_poll_start(handle_, poll_events_, ares_uv_poll_cb);
  }

  bool IsFdStillReadableLocked() override {
    /* uv_poll_t is based on poll, which is level triggered. So, if cares
     * leaves some data unread, the event will trigger again. */
    return false;
  }

  void ShutdownInternalLocked(grpc_error* error) {
    uv_poll_stop(handle_);
    uv_close(reinterpret_cast<uv_handle_t*>(handle_), ares_uv_poll_close_cb);
    if (read_closure_ != nullptr) {
      GRPC_CLOSURE_SCHED(read_closure_, GRPC_ERROR_CANCELLED);
    }
    if (write_closure_ != nullptr) {
      GRPC_CLOSURE_SCHED(write_closure_, GRPC_ERROR_CANCELLED);
    }
  }

  void ShutdownLocked(grpc_error* error) override {
    if (grpc_core::ExecCtx::Get() == nullptr) {
      grpc_core::ExecCtx exec_ctx;
      ShutdownInternalLocked(error);
    } else {
      ShutdownInternalLocked(error);
    }
  }

  ares_socket_t GetWrappedAresSocketLocked() override { return as_; }

  const char* GetName() override { return name_; }

  char* name_;
  ares_socket_t as_;
  uv_poll_t* handle_;
  grpc_closure* read_closure_ = nullptr;
  grpc_closure* write_closure_ = nullptr;
  int poll_events_ = 0;
  grpc_combiner* combiner_;
};

struct AresUvPollCbArg {
  AresUvPollCbArg(uv_poll_t* handle, int status, int events)
      : handle(handle), status(status), events(events) {}

  uv_poll_t* handle;
  int status;
  int events;
};

static void ares_uv_poll_cb_locked(void* arg, grpc_error* error) {
  grpc_core::UniquePtr<AresUvPollCbArg> arg_struct(
      reinterpret_cast<AresUvPollCbArg*>(arg));
  uv_poll_t* handle = arg_struct->handle;
  int status = arg_struct->status;
  int events = arg_struct->events;
  GrpcPolledFdLibuv* polled_fd =
      reinterpret_cast<GrpcPolledFdLibuv*>(handle->data);
  if (status < 0) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("cares polling error");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
  }
  if (events & UV_READABLE) {
    GPR_ASSERT(polled_fd->read_closure_ != nullptr);
    GRPC_CLOSURE_SCHED(polled_fd->read_closure_, error);
    polled_fd->read_closure_ = nullptr;
    polled_fd->poll_events_ &= ~UV_READABLE;
  }
  if (events & UV_WRITABLE) {
    GPR_ASSERT(polled_fd->write_closure_ != nullptr);
    GRPC_CLOSURE_SCHED(polled_fd->write_closure_, error);
    polled_fd->write_closure_ = nullptr;
    polled_fd->poll_events_ &= ~UV_WRITABLE;
  }
  uv_poll_start(handle, polled_fd->poll_events_, ares_uv_poll_cb);
}

void ares_uv_poll_cb(uv_poll_t* handle, int status, int events) {
  grpc_core::ExecCtx exec_ctx;
  GrpcPolledFdLibuv* polled_fd =
      reinterpret_cast<GrpcPolledFdLibuv*>(handle->data);
  AresUvPollCbArg* arg = New<AresUvPollCbArg>(handle, status, events);
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_CREATE(ares_uv_poll_cb_locked, arg,
                          grpc_combiner_scheduler(polled_fd->combiner_)),
      GRPC_ERROR_NONE);
}

class GrpcPolledFdFactoryLibuv : public GrpcPolledFdFactory {
 public:
  GrpcPolledFd* NewGrpcPolledFdLocked(ares_socket_t as,
                                      grpc_pollset_set* driver_pollset_set,
                                      grpc_combiner* combiner) override {
    return New<GrpcPolledFdLibuv>(as, combiner);
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {}
};

UniquePtr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(grpc_combiner* combiner) {
  return UniquePtr<GrpcPolledFdFactory>(New<GrpcPolledFdFactoryLibuv>());
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GRPC_UV) */
