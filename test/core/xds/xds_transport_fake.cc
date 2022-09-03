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

void FakeXdsTransportFactory::FakeStreamingCall::Orphan() {
  {
    MutexLock lock(&mu_);
    // Can't call event_handler_->OnStatusReceived() or unref event_handler_
    // synchronously, since those operations will trigger code in
    // XdsClient that acquires its mutex, but it was already holding its
    // mutex when it called us, so it would deadlock.
    GetDefaultEventEngine()->Run([event_handler = std::move(event_handler_),
                                  status_sent = status_sent_]() mutable {
      ExecCtx exec_ctx;
      if (!status_sent) event_handler->OnStatusReceived(absl::OkStatus());
      event_handler.reset();
    });
    status_sent_ = true;
  }
  Unref();
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessage(
    std::string payload) {
  MutexLock lock(&mu_);
  from_client_messages_.push_back(std::move(payload));
  cv_.Signal();
  // Can't call event_handler_->OnRequestSent() synchronously, since that
  // operation will trigger code in XdsClient that acquires its mutex, but it
  // was already holding its mutex when it called us, so it would deadlock.
  GetDefaultEventEngine()->Run(
      [event_handler = event_handler_->Ref()]() mutable {
        ExecCtx exec_ctx;
        event_handler->OnRequestSent(/*ok=*/true);
        event_handler.reset();
      });
}

absl::optional<std::string>
FakeXdsTransportFactory::FakeStreamingCall::GetMessageFromClient(
    absl::Duration timeout) {
  MutexLock lock(&mu_);
  while (from_client_messages_.empty()) {
    if (cv_.WaitWithTimeout(&mu_, timeout)) return absl::nullopt;
  }
  std::string payload = from_client_messages_.front();
  from_client_messages_.pop_front();
  return payload;
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessageToClient(
    absl::string_view payload) {
  ExecCtx exec_ctx;
  RefCountedPtr<RefCountedEventHandler> event_handler;
  {
    MutexLock lock(&mu_);
    event_handler = event_handler_->Ref();
  }
  event_handler->OnRecvMessage(payload);
}

void FakeXdsTransportFactory::FakeStreamingCall::MaybeSendStatusToClient(
    absl::Status status) {
  ExecCtx exec_ctx;
  RefCountedPtr<RefCountedEventHandler> event_handler;
  {
    MutexLock lock(&mu_);
    if (status_sent_) return;
    status_sent_ = true;
    event_handler = event_handler_->Ref();
  }
  event_handler->OnStatusReceived(std::move(status));
}

//
// FakeXdsTransportFactory::FakeXdsTransport
//

void FakeXdsTransportFactory::FakeXdsTransport::TriggerConnectionFailure(
    absl::Status status) {
  MutexLock lock(&mu_);
  if (on_connectivity_failure_ == nullptr) return;
  on_connectivity_failure_(std::move(status));
}

void FakeXdsTransportFactory::FakeXdsTransport::Orphan() {
  {
    MutexLock lock(&mu_);
    // Can't destroy on_connectivity_failure_ synchronously, since that
    // operation will trigger code in XdsClient that acquires its mutex, but
    // it was already holding its mutex when it called us, so it would deadlock.
    GetDefaultEventEngine()->Run([on_connectivity_failure = std::move(
                                      on_connectivity_failure_)]() mutable {
      ExecCtx exec_ctx;
      on_connectivity_failure = nullptr;  // Destroys it.
    });
  }
  Unref();
}

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
    absl::Status* /*status*/) {
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
FakeXdsTransportFactory::GetStream(const XdsBootstrap::XdsServer& server,
                                   const char* method) {
  auto transport = GetTransport(server);
  return transport->GetStream(method);
}

RefCountedPtr<FakeXdsTransportFactory::FakeXdsTransport>
FakeXdsTransportFactory::GetTransport(const XdsBootstrap::XdsServer& server) {
  MutexLock lock(&mu_);
  RefCountedPtr<FakeXdsTransport> transport = transport_map_[server];
  GPR_ASSERT(transport != nullptr);
  return transport;
}

}  // namespace grpc_core
