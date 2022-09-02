//
// Copyright 2022 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "test/core/xds/xds_transport_fake.h"

#include <functional>
#include <memory>
#include <utility>

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"

using grpc_event_engine::experimental::GetDefaultEventEngine;

namespace grpc_core {

//
// FakeXdsTransportFactory::FakeStreamingCall
//

FakeXdsTransportFactory::FakeStreamingCall::~FakeStreamingCall() {
  GPR_ASSERT(!from_client_message_.has_value());
  if (!status_sent_) {
    event_handler_->OnStatusReceived(absl::OkStatus());
  }
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessage(
    std::string payload) {
  MutexLock lock(&mu_);
  GPR_ASSERT(!from_client_message_.has_value());
  from_client_message_ = std::move(payload);
}

absl::optional<std::string>
FakeXdsTransportFactory::FakeStreamingCall::GetMessageFromClient() {
  MutexLock lock(&mu_);
  if (!from_client_message_.has_value()) return absl::nullopt;
  // Note: Can't use std::move() here, since that counter-intuitively
  // leaves the absl::optional<> containing a moved-from object, and
  // here we need it to be containing nullopt.
  auto from_client_message =
      std::exchange(from_client_message_, absl::nullopt);
  RefCountedPtr<FakeStreamingCall> self = Ref();
  GetDefaultEventEngine()->Run(
      [self = std::move(self)]() {
        ExecCtx exec_ctx;
        self->event_handler_->OnRequestSent(/*ok=*/true);
      });
  return from_client_message;
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessageToClient(
    absl::string_view payload) {
  event_handler_->OnRecvMessage(payload);
}

void FakeXdsTransportFactory::FakeStreamingCall::SendStatusToClient(
    absl::Status status) {
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(!status_sent_);
    status_sent_ = true;
  }
  event_handler_->OnStatusReceived(std::move(status));
}

//
// FakeXdsTransportFactory::FakeXdsTransport
//

RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall>
FakeXdsTransportFactory::FakeXdsTransport::GetStream(const char* method) {
  MutexLock lock(&mu_);
  auto it = active_calls_.find(method);
  if (it == active_calls_.end()) return nullptr;
  return it->second;
}

void FakeXdsTransportFactory::FakeXdsTransport::RemoveStream(
    const char* method, FakeStreamingCall* call) {
  MutexLock lock(&mu_);
  auto it = active_calls_.find(method);
  if (it != active_calls_.end() && it->second.get() == call) {
    active_calls_.erase(it);
  }
}

OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
FakeXdsTransportFactory::FakeXdsTransport::CreateStreamingCall(
    const char* method,
    std::unique_ptr<StreamingCall::EventHandler> event_handler) {
  auto call = MakeOrphanable<FakeStreamingCall>(std::move(event_handler));
  MutexLock lock(&mu_);
  active_calls_[method] = call->Ref();
  return call;
}

//
// FakeXdsTransportFactory
//

constexpr char FakeXdsTransportFactory::kAdsMethod[];

OrphanablePtr<XdsTransportFactory::XdsTransport>
FakeXdsTransportFactory::Create(
    const XdsBootstrap::XdsServer& server,
    std::function<void(absl::Status)> on_connectivity_failure,
    absl::Status* status) {
  auto transport =
      MakeOrphanable<FakeXdsTransport>(std::move(on_connectivity_failure));
  MutexLock lock(&mu_);
  auto& entry = transport_map_[server];
  GPR_ASSERT(entry == nullptr);
  entry = transport->Ref();
  return transport;
}

void FakeXdsTransportFactory::TriggerConnectionFailure(
    const XdsBootstrap::XdsServer& server, absl::Status status) {
  auto transport = GetTransport(server);
  transport->TriggerConnectionFailure(std::move(status));
}

RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall>
FakeXdsTransportFactory::GetStream(
    const XdsBootstrap::XdsServer& server, const char* method) {
  auto transport = GetTransport(server);
  return transport->GetStream(method);
}

RefCountedPtr<FakeXdsTransportFactory::FakeXdsTransport>
FakeXdsTransportFactory::GetTransport(
    const XdsBootstrap::XdsServer& server) {
  MutexLock lock(&mu_);
  RefCountedPtr<FakeXdsTransport> transport = transport_map_[server];
  GPR_ASSERT(transport != nullptr);
  return transport;
}


}  // namespace grpc_core
