// Copyright 2023 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <string>
#include <vector>

#include <ares.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"

namespace grpc_event_engine {
namespace experimental {

#ifdef _WIN32
class WinSocket;
using PollerHandle = std::unique_ptr<WinSocket>;
#else
class EventHandle;
using PollerHandle = EventHandle*;
#endif
using RegisterAresSocketWithPollerCallback =
    absl::AnyInvocable<PollerHandle(ares_socket_t)>;

extern grpc_core::TraceFlag grpc_trace_ares_driver;

#define GRPC_ARES_DRIVER_TRACE_LOG(format, ...)                \
  do {                                                         \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ares_driver)) {     \
      gpr_log(GPR_INFO, "(ares driver) " format, __VA_ARGS__); \
    }                                                          \
  } while (0)

class GrpcAresRequest {
 public:
  virtual bool Cancel() = 0;
};

// A GrpcAresHostnameRequest represents both "A" and "AAAA" (if available)
// lookup
class GrpcAresHostnameRequest : public GrpcAresRequest {
 public:
  using Result = std::vector<EventEngine::ResolvedAddress>;

  // on_resolve is guaranteed to be called with Result or failure status unless
  // being cancelled and the request object's lifetime is managed internally
  // after Start.
  virtual void Start(
      absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) = 0;
};
absl::StatusOr<GrpcAresHostnameRequest*> CreateGrpcAresHostnameRequest(
    absl::string_view name, absl::string_view default_port,
    absl::string_view dns_server, bool check_port,
    EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb,
    EventEngine* event_engine);

class GrpcAresSRVRequest : public GrpcAresRequest {
 public:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

  // on_resolve is guaranteed to be called with Result or failure status unless
  // being cancelled and the request object's lifetime is managed internally
  // after Start.
  virtual void Start(
      absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) = 0;
};
absl::StatusOr<GrpcAresSRVRequest*> CreateGrpcAresSRVRequest(
    absl::string_view name, EventEngine::Duration timeout,
    absl::string_view dns_server, bool check_port,
    RegisterAresSocketWithPollerCallback register_cb,
    EventEngine* event_engine);

class GrpcAresTXTRequest : public GrpcAresRequest {
 public:
  using Result = std::string;

  // on_resolve is guaranteed to be called with Result or failure status unless
  // being cancelled and the request object's lifetime is managed internally
  // after Start.
  virtual void Start(
      absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) = 0;
};
absl::StatusOr<GrpcAresTXTRequest*> CreateGrpcAresTXTRequest(
    absl::string_view name, EventEngine::Duration timeout,
    absl::string_view dns_server, bool check_port,
    RegisterAresSocketWithPollerCallback register_cb,
    EventEngine* event_engine);

}  // namespace experimental
}  // namespace grpc_event_engine

// Exposed in this header for C-core tests only
extern void (*ares_driver_test_only_inject_config)(ares_channel channel);

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H
