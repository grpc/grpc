// Copyright 2021 The gRPC Authors
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

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/port.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/event_engine_internal.h"
#include "src/core/lib/event_engine/sockaddr.h"

namespace grpc_event_engine {
namespace experimental {

EventEngine* g_event_engine = nullptr;
std::function<std::unique_ptr<EventEngine>()>* g_event_engine_factory = nullptr;

EventEngine::ResolvedAddress::ResolvedAddress(const sockaddr* address,
                                              socklen_t size)
    : size_(size) {
  GPR_ASSERT(size <= sizeof(address_));
  memcpy(&address_, address, size);
}

const struct sockaddr* EventEngine::ResolvedAddress::address() const {
  return reinterpret_cast<const struct sockaddr*>(address_);
}

socklen_t EventEngine::ResolvedAddress::size() const { return size_; }

void SetDefaultEventEngineFactory(
    std::function<std::unique_ptr<EventEngine>()>* factory) {
  g_event_engine_factory = factory;
}

std::unique_ptr<EventEngine> CreateEventEngine() {
  if (g_event_engine_factory == nullptr) {
    // TODO(hork): replace with LibuvEventEngineFactory
    abort();
  }
  return (*g_event_engine_factory)();
}

EventEngine* GetDefaultEventEngine() {
  if (g_event_engine == nullptr) {
    g_event_engine = CreateEventEngine().release();
  }
  return g_event_engine;
}

}  // namespace experimental
}  // namespace grpc_event_engine
