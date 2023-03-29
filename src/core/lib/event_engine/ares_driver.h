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

#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <ares.h>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

#include "include/grpc/event_engine/event_engine.h"

#include "src/core/lib/gprpp/ref_counted.h"

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

// An inflight name service lookup request
class GrpcAresRequest : public grpc_core::RefCounted<GrpcAresRequest> {
 public:
  virtual absl::Status Initialize(absl::string_view dns_server,
                                  bool check_port) = 0;
  virtual void Cancel() = 0;

 protected:
  GrpcAresRequest();
};

// A GrpcAresHostnameRequest represents both "A" and "AAAA" (if available)
// lookup
class GrpcAresHostnameRequest : public virtual GrpcAresRequest {
 protected:
  using Result = std::vector<EventEngine::ResolvedAddress>;

 public:
  virtual void Start(
      absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) = 0;
};
GrpcAresHostnameRequest* CreateGrpcAresHostnameRequest(
    absl::string_view name, absl::string_view default_port,
    EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb,
    EventEngine* event_engine);

class GrpcAresSRVRequest : public virtual GrpcAresRequest {
 protected:
  using Result = std::vector<EventEngine::DNSResolver::SRVRecord>;

 public:
  virtual void Start(
      absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) = 0;
};
GrpcAresSRVRequest* CreateGrpcAresSRVRequest(
    absl::string_view name, EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb,
    EventEngine* event_engine);

class GrpcAresTXTRequest : public virtual GrpcAresRequest {
 protected:
  using Result = std::string;

 public:
  virtual void Start(
      absl::AnyInvocable<void(absl::StatusOr<Result>)> on_resolve) = 0;
};
GrpcAresTXTRequest* CreateGrpcAresTXTRequest(
    absl::string_view name, EventEngine::Duration timeout,
    RegisterAresSocketWithPollerCallback register_cb,
    EventEngine* event_engine);

}  // namespace experimental
}  // namespace grpc_event_engine

// Exposed in this header for C-core tests only
extern void (*ares_driver_test_only_inject_config)(ares_channel channel);

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ARES_DRIVER_H
