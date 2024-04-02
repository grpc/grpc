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

#include "test/cpp/end2end/connection_attempt_injector.h"

#include <memory>

#include "absl/memory/memory.h"
#include "absl/utility/utility.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"

// defined in tcp_client.cc
extern grpc_tcp_client_vtable* grpc_tcp_client_impl;

using ::grpc_event_engine::experimental::EndpointConfig;

namespace grpc {
namespace testing {

//
// ConnectionAttemptInjector static setup
//

namespace {

grpc_tcp_client_vtable* g_original_vtable = nullptr;

grpc_core::Mutex* g_mu = nullptr;
ConnectionAttemptInjector* g_injector ABSL_GUARDED_BY(*g_mu) = nullptr;

}  // namespace

grpc_tcp_client_vtable ConnectionAttemptInjector::kDelayedConnectVTable = {
    ConnectionAttemptInjector::TcpConnect,
    ConnectionAttemptInjector::TcpConnectCancel};

void ConnectionAttemptInjector::Init() {
  g_mu = new grpc_core::Mutex();
  g_original_vtable = grpc_tcp_client_impl;
  grpc_tcp_client_impl = &kDelayedConnectVTable;
}

int64_t ConnectionAttemptInjector::TcpConnect(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  grpc_core::MutexLock lock(g_mu);
  // If there's no injector, use the original vtable.
  if (g_injector == nullptr) {
    g_original_vtable->connect(closure, ep, interested_parties, config, addr,
                               deadline);
    return 0;
  }
  // Otherwise, use the injector.
  g_injector->HandleConnection(closure, ep, interested_parties, config, addr,
                               deadline);
  return 0;
}

// TODO(vigneshbabu): This method should check whether the connect attempt has
// actually been started, and if so, it should call
// g_original_vtable->cancel_connect(). If the attempt has not actually been
// started, it should mark the connect request as cancelled, so that when the
// request is resumed, it will not actually proceed.
bool ConnectionAttemptInjector::TcpConnectCancel(
    int64_t /*connection_handle*/) {
  return false;
}

//
// ConnectionAttemptInjector instance
//

ConnectionAttemptInjector::ConnectionAttemptInjector() {
  // Fail if ConnectionAttemptInjector::Init() was not called after
  // grpc_init() to inject the vtable.
  GPR_ASSERT(grpc_tcp_client_impl == &kDelayedConnectVTable);
  grpc_core::MutexLock lock(g_mu);
  GPR_ASSERT(g_injector == nullptr);
  g_injector = this;
}

ConnectionAttemptInjector::~ConnectionAttemptInjector() {
  grpc_core::MutexLock lock(g_mu);
  g_injector = nullptr;
}

std::unique_ptr<ConnectionAttemptInjector::Hold>
ConnectionAttemptInjector::AddHold(int port, bool intercept_completion) {
  grpc_core::MutexLock lock(&mu_);
  auto hold = std::make_unique<Hold>(this, port, intercept_completion);
  holds_.push_back(hold.get());
  return hold;
}

void ConnectionAttemptInjector::SetDelay(grpc_core::Duration delay) {
  grpc_core::MutexLock lock(&mu_);
  delay_ = delay;
}

void ConnectionAttemptInjector::HandleConnection(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  const int port = grpc_sockaddr_get_port(addr);
  gpr_log(GPR_INFO, "==> HandleConnection(): port=%d", port);
  {
    grpc_core::MutexLock lock(&mu_);
    // First, check if there's a hold request for this port.
    for (auto it = holds_.begin(); it != holds_.end(); ++it) {
      Hold* hold = *it;
      if (port == hold->port_) {
        gpr_log(GPR_INFO, "*** INTERCEPTING CONNECTION ATTEMPT");
        if (hold->intercept_completion_) {
          hold->original_on_complete_ = closure;
          closure = GRPC_CLOSURE_INIT(&hold->on_complete_, Hold::OnComplete,
                                      hold, nullptr);
        }
        hold->queued_attempt_ = std::make_unique<QueuedAttempt>(
            closure, ep, interested_parties, config, addr, deadline);
        hold->start_cv_.Signal();
        holds_.erase(it);
        return;
      }
    }
    // Otherwise, if there's a configured delay, impose it.
    if (delay_.has_value()) {
      new InjectedDelay(*delay_, closure, ep, interested_parties, config, addr,
                        deadline);
      return;
    }
  }
  // Anything we're not holding or delaying should proceed normally.
  g_original_vtable->connect(closure, ep, interested_parties, config, addr,
                             deadline);
}

//
// ConnectionAttemptInjector::QueuedAttempt
//

ConnectionAttemptInjector::QueuedAttempt::QueuedAttempt(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline)
    : closure_(closure),
      endpoint_(ep),
      interested_parties_(interested_parties),
      config_(*reinterpret_cast<const grpc_event_engine::experimental::
                                    ChannelArgsEndpointConfig*>(&config)),
      deadline_(deadline) {
  memcpy(&address_, addr, sizeof(address_));
}

ConnectionAttemptInjector::QueuedAttempt::~QueuedAttempt() {
  GPR_ASSERT(closure_ == nullptr);
}

void ConnectionAttemptInjector::QueuedAttempt::Resume() {
  GPR_ASSERT(closure_ != nullptr);
  g_original_vtable->connect(closure_, endpoint_, interested_parties_, config_,
                             &address_, deadline_);
  closure_ = nullptr;
}

void ConnectionAttemptInjector::QueuedAttempt::Fail(grpc_error_handle error) {
  GPR_ASSERT(closure_ != nullptr);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure_, error);
  closure_ = nullptr;
}

//
// ConnectionAttemptInjector::InjectedDelay
//

ConnectionAttemptInjector::InjectedDelay::InjectedDelay(
    grpc_core::Duration duration, grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline)
    : attempt_(closure, ep, interested_parties, config, addr, deadline) {
  grpc_core::Timestamp now = grpc_core::Timestamp::Now();
  duration = std::min(duration, deadline - now);
  grpc_event_engine::experimental::GetDefaultEventEngine()->RunAfter(
      duration, [this] {
        grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        TimerCallback();
      });
}

void ConnectionAttemptInjector::InjectedDelay::TimerCallback() {
  attempt_.Resume();
  delete this;
}

//
// ConnectionAttemptInjector::Hold
//

ConnectionAttemptInjector::Hold::Hold(ConnectionAttemptInjector* injector,
                                      int port, bool intercept_completion)
    : injector_(injector),
      port_(port),
      intercept_completion_(intercept_completion) {}

void ConnectionAttemptInjector::Hold::Wait() {
  gpr_log(GPR_INFO, "=== WAITING FOR CONNECTION ATTEMPT ON PORT %d ===", port_);
  grpc_core::MutexLock lock(&injector_->mu_);
  while (queued_attempt_ == nullptr) {
    start_cv_.Wait(&injector_->mu_);
  }
  gpr_log(GPR_INFO, "=== CONNECTION ATTEMPT STARTED ON PORT %d ===", port_);
}

void ConnectionAttemptInjector::Hold::Resume() {
  gpr_log(GPR_INFO, "=== RESUMING CONNECTION ATTEMPT ON PORT %d ===", port_);
  grpc_core::ExecCtx exec_ctx;
  std::unique_ptr<QueuedAttempt> attempt;
  {
    grpc_core::MutexLock lock(&injector_->mu_);
    attempt = std::move(queued_attempt_);
  }
  attempt->Resume();
}

void ConnectionAttemptInjector::Hold::Fail(grpc_error_handle error) {
  gpr_log(GPR_INFO, "=== FAILING CONNECTION ATTEMPT ON PORT %d ===", port_);
  grpc_core::ExecCtx exec_ctx;
  std::unique_ptr<QueuedAttempt> attempt;
  {
    grpc_core::MutexLock lock(&injector_->mu_);
    attempt = std::move(queued_attempt_);
  }
  attempt->Fail(error);
}

void ConnectionAttemptInjector::Hold::WaitForCompletion() {
  gpr_log(GPR_INFO,
          "=== WAITING FOR CONNECTION COMPLETION ON PORT %d ===", port_);
  grpc_core::MutexLock lock(&injector_->mu_);
  while (original_on_complete_ != nullptr) {
    complete_cv_.Wait(&injector_->mu_);
  }
  gpr_log(GPR_INFO, "=== CONNECTION COMPLETED ON PORT %d ===", port_);
}

bool ConnectionAttemptInjector::Hold::IsStarted() {
  grpc_core::MutexLock lock(&injector_->mu_);
  return !start_cv_.WaitWithDeadline(&injector_->mu_, absl::Now());
}

void ConnectionAttemptInjector::Hold::OnComplete(void* arg,
                                                 grpc_error_handle error) {
  auto* self = static_cast<Hold*>(arg);
  grpc_closure* on_complete;
  {
    grpc_core::MutexLock lock(&self->injector_->mu_);
    on_complete = self->original_on_complete_;
    self->original_on_complete_ = nullptr;
    self->complete_cv_.Signal();
  }
  grpc_core::Closure::Run(DEBUG_LOCATION, on_complete, error);
}

}  // namespace testing
}  // namespace grpc
