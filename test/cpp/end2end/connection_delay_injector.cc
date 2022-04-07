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

// defined in tcp_client.cc
extern grpc_tcp_client_vtable* grpc_tcp_client_impl;

namespace grpc {
namespace testing {

//
// ConnectionAttemptInjector
//

namespace {

grpc_tcp_client_vtable* g_original_vtable = nullptr;
ConnectionAttemptInjector* g_injector = nullptr;

void TcpConnectWithDelay(grpc_closure* closure, grpc_endpoint** ep,
                         grpc_pollset_set* interested_parties,
                         const grpc_channel_args* channel_args,
                         const grpc_resolved_address* addr,
                         grpc_core::Timestamp deadline) {
  GPR_ASSERT(g_injector != nullptr);
  g_injector->HandleConnection(closure, ep, interested_parties, channel_args,
                               addr, deadline);
}

grpc_tcp_client_vtable kDelayedConnectVTable = {TcpConnectWithDelay};

}  // namespace

ConnectionAttemptInjector::ConnectionAttemptInjector() {
  g_original_vtable =
      absl::exchange(grpc_tcp_client_impl, &kDelayedConnectVTable);
  GPR_ASSERT(absl::exchange(g_injector, this) == nullptr);
}

ConnectionAttemptInjector::~ConnectionAttemptInjector() {
  g_injector = nullptr;
  grpc_tcp_client_impl = g_original_vtable;
}

void ConnectionAttemptInjector::AttemptConnection(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties,
    const grpc_channel_args* channel_args, const grpc_resolved_address* addr,
    grpc_core::Timestamp deadline) {
  g_original_vtable->connect(closure, ep, interested_parties, channel_args,
                             addr, deadline);
}

//
// ConnectionDelayInjector
//

class ConnectionDelayInjector::InjectedDelay {
 public:
  InjectedDelay(grpc_core::Duration duration, grpc_closure* closure,
                grpc_endpoint** ep, grpc_pollset_set* interested_parties,
                const grpc_channel_args* channel_args,
                const grpc_resolved_address* addr,
                grpc_core::Timestamp deadline)
      : closure_(closure),
        endpoint_(ep),
        interested_parties_(interested_parties),
        channel_args_(grpc_channel_args_copy(channel_args)),
        deadline_(deadline) {
    memcpy(&address_, addr, sizeof(grpc_resolved_address));
    GRPC_CLOSURE_INIT(&timer_callback_, TimerCallback, this, nullptr);
    deadline_ += duration;
    grpc_timer_init(&timer_, grpc_core::ExecCtx::Get()->Now() + duration,
                    &timer_callback_);
  }

  ~InjectedDelay() { grpc_channel_args_destroy(channel_args_); }

 private:
  static void TimerCallback(void* arg, grpc_error_handle /*error*/) {
    auto* self = static_cast<InjectedDelay*>(arg);
    AttemptConnection(self->closure_, self->endpoint_,
                      self->interested_parties_, self->channel_args_,
                      &self->address_, self->deadline_);
    delete self;
  }

  grpc_timer timer_;
  grpc_closure timer_callback_;

  // Original args.
  grpc_closure* closure_;
  grpc_endpoint** endpoint_;
  grpc_pollset_set* interested_parties_;
  const grpc_channel_args* channel_args_;
  grpc_resolved_address address_;
  grpc_core::Timestamp deadline_;
};

void ConnectionDelayInjector::HandleConnection(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties,
    const grpc_channel_args* channel_args, const grpc_resolved_address* addr,
    grpc_core::Timestamp deadline) {
  new InjectedDelay(duration_, closure, ep, interested_parties, channel_args,
                    addr, deadline);
}

}  // namespace testing
}  // namespace grpc
