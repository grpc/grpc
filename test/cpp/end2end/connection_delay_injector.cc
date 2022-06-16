// Copyright 2016 gRPC authors.
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

#include "test/cpp/end2end/connection_delay_injector.h"

#include <memory>

#include "absl/memory/memory.h"
#include "absl/utility/utility.h"

#include "src/core/lib/event_engine/map_backed_endpoint_config.h"
#include "src/core/lib/gprpp/sync.h"

// defined in tcp_client.cc
extern grpc_tcp_client_vtable* grpc_tcp_client_impl;

using ::grpc_event_engine::experimental::ConfigMap;
using ::grpc_event_engine::experimental::EndpointConfig;

namespace grpc {
namespace testing {

ConfigMap CopyFromEndpointConfig(const EndpointConfig& config) {
  ConfigMap map;
  map.CopyFrom(config, GRPC_ARG_TCP_READ_CHUNK_SIZE);
  map.CopyFrom(config, GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE);
  map.CopyFrom(config, GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE);
  map.CopyFrom(config, GRPC_ARG_KEEPALIVE_TIME_MS);
  map.CopyFrom(config, GRPC_ARG_KEEPALIVE_TIMEOUT_MS);
  map.CopyFrom(config, GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD);
  map.CopyFrom(config, GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS);
  map.CopyFrom(config, GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED);
  map.CopyFrom(config, GRPC_ARG_SOCKET_MUTATOR);
  map.CopyFrom(config, GRPC_ARG_ALLOW_REUSEPORT);
  map.CopyFrom(config, GRPC_ARG_EXPAND_WILDCARD_ADDRS);
  map.CopyFrom(config, GRPC_ARG_RESOURCE_QUOTA);
  return map;
}

//
// ConnectionAttemptInjector
//

namespace {

grpc_tcp_client_vtable* g_original_vtable = nullptr;

grpc_core::Mutex* g_mu = nullptr;
ConnectionAttemptInjector* g_injector ABSL_GUARDED_BY(*g_mu) = nullptr;

int64_t TcpConnectWithDelay(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties,
    const grpc_event_engine::experimental::EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  grpc_core::MutexLock lock(g_mu);
  if (g_injector == nullptr) {
    g_original_vtable->connect(closure, ep, interested_parties, config, addr,
                               deadline);
    return 0;
  }
  g_injector->HandleConnection(closure, ep, interested_parties, config, addr,
                               deadline);
  return 0;
}

// TODO(vigneshbabu): This method should check whether the connect attempt has
// actually been started, and if so, it should call
// g_original_vtable->cancel_connect(). If the attempt has not actually been
// started, it should mark the connect request as cancelled, so that when the
// request is resumed, it will not actually proceed.
bool TcpConnectCancel(int64_t /*connection_handle*/) { return false; }

grpc_tcp_client_vtable kDelayedConnectVTable = {TcpConnectWithDelay,
                                                TcpConnectCancel};

}  // namespace

void ConnectionAttemptInjector::Init() {
  g_mu = new grpc_core::Mutex();
  g_original_vtable = grpc_tcp_client_impl;
  grpc_tcp_client_impl = &kDelayedConnectVTable;
}

ConnectionAttemptInjector::~ConnectionAttemptInjector() {
  grpc_core::MutexLock lock(g_mu);
  g_injector = nullptr;
}

void ConnectionAttemptInjector::Start() {
  // Fail if ConnectionAttemptInjector::Init() was not called after
  // grpc_init() to inject the vtable.
  GPR_ASSERT(grpc_tcp_client_impl == &kDelayedConnectVTable);
  grpc_core::MutexLock lock(g_mu);
  GPR_ASSERT(g_injector == nullptr);
  g_injector = this;
}

void ConnectionAttemptInjector::AttemptConnection(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  g_original_vtable->connect(closure, ep, interested_parties, config, addr,
                             deadline);
}

//
// ConnectionAttemptInjector::InjectedDelay
//

ConnectionAttemptInjector::InjectedDelay::InjectedDelay(
    grpc_core::Duration duration, grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline)
    : attempt_(closure, ep, interested_parties, config, addr, deadline) {
  GRPC_CLOSURE_INIT(&timer_callback_, TimerCallback, this, nullptr);
  grpc_core::Timestamp now = grpc_core::ExecCtx::Get()->Now();
  duration = std::min(duration, deadline - now);
  grpc_timer_init(&timer_, now + duration, &timer_callback_);
}

void ConnectionAttemptInjector::InjectedDelay::TimerCallback(
    void* arg, grpc_error_handle /*error*/) {
  auto* self = static_cast<InjectedDelay*>(arg);
  self->BeforeResumingAction();
  self->attempt_.Resume();
  delete self;
}

//
// ConnectionDelayInjector
//

void ConnectionDelayInjector::HandleConnection(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties,
    const grpc_event_engine::experimental::EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  new InjectedDelay(duration_, closure, ep, interested_parties, config, addr,
                    deadline);
}

}  // namespace testing
}  // namespace grpc
