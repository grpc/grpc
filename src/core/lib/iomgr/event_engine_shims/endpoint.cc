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
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"

#include <memory>

#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/event_engine_shims/resolved_address.h"
#include "src/core/lib/transport/error_utils.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#endif

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace grpc_event_engine {
namespace experimental {

namespace {

void endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool /* urgent */,
                   int /* min_progress_size */) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_CANCELLED);
    return;
  }
  SliceBuffer* read_buffer = new (&eeep->read_buffer) SliceBuffer(slices);
  eeep->endpoint->Read(
      [eeep, cb](absl::Status status) {
        auto* read_buffer = reinterpret_cast<SliceBuffer*>(&eeep->read_buffer);
        read_buffer->~SliceBuffer();
        grpc_core::ExecCtx exec_ctx;
        grpc_core::Closure::Run(DEBUG_LOCATION, cb,
                                absl_status_to_grpc_error(status));
        exec_ctx.Flush();
      },
      read_buffer, nullptr);
}

void endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, void* arg, int /*max_frame_size*/) {
  // TODO(hork): adapt arg to some metrics collection mechanism.
  (void)arg;
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_CANCELLED);
    return;
  }
  SliceBuffer* write_buffer = new (&eeep->write_buffer) SliceBuffer(slices);
  eeep->endpoint->Write(
      [eeep, cb](absl::Status status) {
        auto* write_buffer =
            reinterpret_cast<SliceBuffer*>(&eeep->write_buffer);
        write_buffer->~SliceBuffer();
        grpc_core::ExecCtx exec_ctx;
        grpc_core::Closure::Run(DEBUG_LOCATION, cb,
                                absl_status_to_grpc_error(status));
        exec_ctx.Flush();
      },
      write_buffer, nullptr);
}
void endpoint_add_to_pollset(grpc_endpoint* /* ep */,
                             grpc_pollset* /* pollset */) {}
void endpoint_add_to_pollset_set(grpc_endpoint* /* ep */,
                                 grpc_pollset_set* /* pollset */) {}
void endpoint_delete_from_pollset_set(grpc_endpoint* /* ep */,
                                      grpc_pollset_set* /* pollset */) {}
/// After shutdown, all endpoint operations except destroy are no-op,
/// and will return some kind of sane default (empty strings, nullptrs, etc). It
/// is the caller's responsibility to ensure that calls to endpoint_shutdown are
/// synchronized.
void endpoint_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    std::string str = grpc_error_std_string(why);
    gpr_log(GPR_INFO, "TCP Endpoint %p shutdown why=%s", eeep->endpoint.get(),
            str.c_str());
  }
  eeep->endpoint.reset();
}

void endpoint_destroy(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  delete eeep;
}

absl::string_view endpoint_get_peer(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    return "";
  }
  if (eeep->peer_address.empty()) {
    const EventEngine::ResolvedAddress& addr = eeep->endpoint->GetPeerAddress();
    // DO NOT SUBMIT - handle invalid addresses
    eeep->peer_address = ResolvedAddressToURI(addr).value();
  }
  return eeep->peer_address;
}

absl::string_view endpoint_get_local_address(grpc_endpoint* ep) {
  auto* eeep = reinterpret_cast<grpc_event_engine_endpoint*>(ep);
  if (eeep->endpoint == nullptr) {
    return "";
  }
  if (eeep->local_address.empty()) {
    const EventEngine::ResolvedAddress& addr =
        eeep->endpoint->GetLocalAddress();
    // DO NOT SUBMIT - handle invalid addresses
    eeep->local_address = ResolvedAddressToURI(addr).value();
  }
  return eeep->local_address;
}

int endpoint_get_fd(grpc_endpoint* /* ep */) { return -1; }

bool endpoint_can_track_err(grpc_endpoint* /* ep */) { return false; }

grpc_endpoint_vtable grpc_event_engine_endpoint_vtable = {
    endpoint_read,
    endpoint_write,
    endpoint_add_to_pollset,
    endpoint_add_to_pollset_set,
    endpoint_delete_from_pollset_set,
    endpoint_shutdown,
    endpoint_destroy,
    endpoint_get_peer,
    endpoint_get_local_address,
    endpoint_get_fd,
    endpoint_can_track_err};

}  // namespace

grpc_event_engine_endpoint* grpc_event_engine_tcp_server_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint) {
  auto endpoint = new grpc_event_engine_endpoint;
  endpoint->base.vtable = &grpc_event_engine_endpoint_vtable;
  endpoint->endpoint = std::move(ee_endpoint);
  return endpoint;
}

// TESTING ONLY: Uses the EventEngine as a scheduler.
class EventEngineScheduler : public posix_engine::Scheduler {
 public:
  explicit EventEngineScheduler(std::shared_ptr<PosixEventEngine> engine)
      : engine_(std::move(engine)) {}

  void Run(experimental::EventEngine::Closure* closure) override {
    engine_->Run(closure);
  }
  void Run(absl::AnyInvocable<void()> cb) override {
    engine_->Run(std::move(cb));
  }

 private:
  std::shared_ptr<EventEngine> engine_;
};

#ifdef GRPC_POSIX_SOCKET_TCP
grpc_endpoint* CreatePosixIomgrEndpiont(
    grpc_fd* wrapped_fd,
    const grpc_event_engine::experimental::EndpointConfig& endpoint_config,
    absl::string_view peer_string) {
  auto engine =
      std::dynamic_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
  auto* scheduler = new EventEngineScheduler(engine);
  auto handle = posix_engine::GetDefaultPoller(scheduler)->CreateHandle(
      grpc_fd_wrapped_fd(wrapped_fd), peer_string, /* track_err= */ false);
  auto endpoint = grpc_event_engine_tcp_server_endpoint_create(
      absl::make_unique<posix_engine::PosixEndpoint>(
          handle,
          new posix_engine::PosixEngineClosure(
              [scheduler, peer_string = std::string(peer_string)](
                  absl::Status /*dont_care*/) {
                gpr_log(GPR_DEBUG, "DO NOT SUBMIT: deleting endpoint for %s",
                        peer_string.c_str());
                delete scheduler;
              },
              /*is_permanent=*/false),
          engine, endpoint_config));
  return &endpoint->base;
}
#endif

}  // namespace experimental
}  // namespace grpc_event_engine
