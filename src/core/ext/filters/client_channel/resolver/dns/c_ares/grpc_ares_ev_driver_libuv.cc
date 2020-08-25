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

#include "absl/strings/str_format.h"

#include <ares.h>
#include <uv.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

void ares_uv_poll_cb(uv_poll_t* handle, int status, int events);

void ares_uv_poll_close_cb(uv_handle_t* handle) { delete handle; }

class GrpcPolledFdLibuv : public GrpcPolledFd {
 public:
  GrpcPolledFdLibuv(ares_socket_t as,
                    std::shared_ptr<WorkSerializer> work_serializer)
      : name_(absl::StrFormat("c-ares socket: %" PRIdPTR, (intptr_t)as)),
        as_(as),
        work_serializer_(std::move(work_serializer)) {
    handle_ = new uv_poll_t();
    uv_poll_init_socket(uv_default_loop(), handle_, as);
    handle_->data = this;
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
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, read_closure_,
                              GRPC_ERROR_CANCELLED);
    }
    if (write_closure_ != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, write_closure_,
                              GRPC_ERROR_CANCELLED);
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

  const char* GetName() override { return name_.c_str(); }

  // TODO(apolcyn): Data members should be private.
  std::string name_;
  ares_socket_t as_;
  uv_poll_t* handle_;
  grpc_closure* read_closure_ = nullptr;
  grpc_closure* write_closure_ = nullptr;
  int poll_events_ = 0;
  std::shared_ptr<WorkSerializer> work_serializer_;
};

struct AresUvPollCbArg {
  AresUvPollCbArg(uv_poll_t* handle, int status, int events)
      : handle(handle), status(status), events(events) {}

  uv_poll_t* handle;
  int status;
  int events;
};

static void ares_uv_poll_cb_locked(AresUvPollCbArg* arg) {
  std::unique_ptr<AresUvPollCbArg> arg_struct(arg);
  uv_poll_t* handle = arg_struct->handle;
  int status = arg_struct->status;
  int events = arg_struct->events;
  GrpcPolledFdLibuv* polled_fd =
      reinterpret_cast<GrpcPolledFdLibuv*>(handle->data);
  grpc_error* error = GRPC_ERROR_NONE;
  if (status < 0) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("cares polling error");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
  }
  if (events & UV_READABLE) {
    GPR_ASSERT(polled_fd->read_closure_ != nullptr);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, polled_fd->read_closure_, error);
    polled_fd->read_closure_ = nullptr;
    polled_fd->poll_events_ &= ~UV_READABLE;
  }
  if (events & UV_WRITABLE) {
    GPR_ASSERT(polled_fd->write_closure_ != nullptr);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, polled_fd->write_closure_, error);
    polled_fd->write_closure_ = nullptr;
    polled_fd->poll_events_ &= ~UV_WRITABLE;
  }
  uv_poll_start(handle, polled_fd->poll_events_, ares_uv_poll_cb);
}

void ares_uv_poll_cb(uv_poll_t* handle, int status, int events) {
  grpc_core::ExecCtx exec_ctx;
  GrpcPolledFdLibuv* polled_fd =
      reinterpret_cast<GrpcPolledFdLibuv*>(handle->data);
  AresUvPollCbArg* arg = new AresUvPollCbArg(handle, status, events);
  polled_fd->work_serializer_->Run([arg]() { ares_uv_poll_cb_locked(arg); },
                                   DEBUG_LOCATION);
}

class GrpcPolledFdFactoryLibuv : public GrpcPolledFdFactory {
 public:
  GrpcPolledFd* NewGrpcPolledFdLocked(
      ares_socket_t as, grpc_pollset_set* driver_pollset_set,
      std::shared_ptr<WorkSerializer> work_serializer) override {
    return new GrpcPolledFdLibuv(as, std::move(work_serializer));
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {}
};

std::unique_ptr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(
    std::shared_ptr<WorkSerializer> work_serializer) {
  return absl::make_unique<GrpcPolledFdFactoryLibuv>();
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GRPC_UV) */
