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

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/sync.h"

// defined in tcp_client.cc
extern grpc_tcp_client_vtable* grpc_tcp_client_impl;

namespace grpc {
namespace testing {

//
// ConnectionAttemptInjector
//

namespace {

grpc_tcp_client_vtable* g_original_vtable = nullptr;

grpc_core::Mutex* g_mu = nullptr;
ConnectionAttemptInjector* g_injector ABSL_GUARDED_BY(*g_mu) = nullptr;

int64_t TcpConnectWithDelay(grpc_closure* closure, grpc_endpoint** ep,
                            grpc_pollset_set* interested_parties,
                            const grpc_channel_args* channel_args,
                            const grpc_resolved_address* addr,
                            grpc_core::Timestamp deadline) {
  grpc_core::MutexLock lock(g_mu);
  if (g_injector == nullptr) {
    g_original_vtable->connect(closure, ep, interested_parties, channel_args,
                               addr, deadline);
    return 0;
  }
  g_injector->HandleConnection(closure, ep, interested_parties, channel_args,
                               addr, deadline);
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
    grpc_pollset_set* interested_parties, const grpc_channel_args* channel_args,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  g_original_vtable->connect(closure, ep, interested_parties, channel_args,
                             addr, deadline);
}

//
// ConnectionAttemptInjector::InjectedDelay
//

ConnectionAttemptInjector::InjectedDelay::InjectedDelay(
    grpc_core::Duration duration, grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const grpc_channel_args* channel_args,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline)
    : attempt_(closure, ep, interested_parties, channel_args, addr, deadline) {
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
    grpc_pollset_set* interested_parties, const grpc_channel_args* channel_args,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  new InjectedDelay(duration_, closure, ep, interested_parties, channel_args,
                    addr, deadline);
}

//
// ConnectionHoldInjector::Hold
//

ConnectionHoldInjector::Hold::Hold(ConnectionHoldInjector* injector, int port,
                                   bool intercept_completion)
    : injector_(injector),
      port_(port),
      intercept_completion_(intercept_completion) {}

void ConnectionHoldInjector::Hold::Wait() {
  gpr_log(GPR_INFO, "=== WAITING FOR CONNECTION ATTEMPT ON PORT %d ===", port_);
  grpc_core::MutexLock lock(&injector_->mu_);
  while (queued_attempt_ == nullptr) {
    start_cv_.Wait(&injector_->mu_);
  }
  gpr_log(GPR_INFO, "=== CONNECTION ATTEMPT STARTED ON PORT %d ===", port_);
}

void ConnectionHoldInjector::Hold::Resume() {
  gpr_log(GPR_INFO, "=== RESUMING CONNECTION ATTEMPT ON PORT %d ===", port_);
  grpc_core::ExecCtx exec_ctx;
  std::unique_ptr<QueuedAttempt> attempt;
  {
    grpc_core::MutexLock lock(&injector_->mu_);
    attempt = std::move(queued_attempt_);
  }
  attempt->Resume();
}

void ConnectionHoldInjector::Hold::Fail(grpc_error_handle error) {
  gpr_log(GPR_INFO, "=== FAILING CONNECTION ATTEMPT ON PORT %d ===", port_);
  grpc_core::ExecCtx exec_ctx;
  std::unique_ptr<QueuedAttempt> attempt;
  {
    grpc_core::MutexLock lock(&injector_->mu_);
    attempt = std::move(queued_attempt_);
  }
  attempt->Fail(error);
}

void ConnectionHoldInjector::Hold::WaitForCompletion() {
  gpr_log(GPR_INFO,
          "=== WAITING FOR CONNECTION COMPLETION ON PORT %d ===", port_);
  grpc_core::MutexLock lock(&injector_->mu_);
  while (original_on_complete_ != nullptr) {
    complete_cv_.Wait(&injector_->mu_);
  }
  gpr_log(GPR_INFO, "=== CONNECTION COMPLETED ON PORT %d ===", port_);
}

bool ConnectionHoldInjector::Hold::IsStarted() {
  grpc_core::MutexLock lock(&injector_->mu_);
  return !start_cv_.WaitWithDeadline(&injector_->mu_, absl::Now());
}

void ConnectionHoldInjector::Hold::OnComplete(void* arg,
                                              grpc_error_handle error) {
  auto* self = static_cast<Hold*>(arg);
  grpc_closure* on_complete;
  {
    grpc_core::MutexLock lock(&self->injector_->mu_);
    on_complete = self->original_on_complete_;
    self->original_on_complete_ = nullptr;
    self->complete_cv_.Signal();
  }
  grpc_core::Closure::Run(DEBUG_LOCATION, on_complete, GRPC_ERROR_REF(error));
}

//
// ConnectionHoldInjector
//

std::unique_ptr<ConnectionHoldInjector::Hold> ConnectionHoldInjector::AddHold(
    int port, bool intercept_completion) {
  grpc_core::MutexLock lock(&mu_);
  auto hold = absl::make_unique<Hold>(this, port, intercept_completion);
  holds_.push_back(hold.get());
  return hold;
}

void ConnectionHoldInjector::HandleConnection(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const grpc_channel_args* channel_args,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  const int port = grpc_sockaddr_get_port(addr);
  gpr_log(GPR_INFO, "==> HandleConnection(): port=%d", port);
  {
    grpc_core::MutexLock lock(&mu_);
    for (auto it = holds_.begin(); it != holds_.end(); ++it) {
      Hold* hold = *it;
      if (port == hold->port_) {
        gpr_log(GPR_INFO, "*** INTERCEPTING CONNECTION ATTEMPT");
        if (hold->intercept_completion_) {
          hold->original_on_complete_ = closure;
          closure = GRPC_CLOSURE_INIT(&hold->on_complete_, Hold::OnComplete,
                                      hold, nullptr);
        }
        hold->queued_attempt_ = absl::make_unique<QueuedAttempt>(
            closure, ep, interested_parties, channel_args, addr, deadline);
        hold->start_cv_.Signal();
        holds_.erase(it);
        return;
      }
    }
  }
  // Anything we're not holding should proceed normally.
  AttemptConnection(closure, ep, interested_parties, channel_args, addr,
                    deadline);
}

}  // namespace testing
}  // namespace grpc
