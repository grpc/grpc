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

#include <stdint.h>

#include <memory>
#include <utility>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

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
  transport_->RemoveStream(method_, this);
  Unref();
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessage(
    std::string payload) {
  MutexLock lock(&mu_);
  from_client_messages_.push_back(std::move(payload));
  cv_.Signal();
  if (transport_->auto_complete_messages_from_client()) {
    CompleteSendMessageFromClientLocked(/*ok=*/true);
  }
}

bool FakeXdsTransportFactory::FakeStreamingCall::HaveMessageFromClient() {
  MutexLock lock(&mu_);
  return !from_client_messages_.empty();
}

absl::optional<std::string>
FakeXdsTransportFactory::FakeStreamingCall::WaitForMessageFromClient(
    absl::Duration timeout) {
  MutexLock lock(&mu_);
  while (from_client_messages_.empty()) {
    if (cv_.WaitWithTimeout(&mu_, timeout * grpc_test_slowdown_factor())) {
      return absl::nullopt;
    }
  }
  std::string payload = from_client_messages_.front();
  from_client_messages_.pop_front();
  return payload;
}

void FakeXdsTransportFactory::FakeStreamingCall::
    CompleteSendMessageFromClientLocked(bool ok) {
  // Can't call event_handler_->OnRequestSent() synchronously, since that
  // operation will trigger code in XdsClient that acquires its mutex, but it
  // was already holding its mutex when it called us, so it would deadlock.
  GetDefaultEventEngine()->Run(
      [event_handler = event_handler_->Ref(), ok]() mutable {
        ExecCtx exec_ctx;
        event_handler->OnRequestSent(ok);
        event_handler.reset();
      });
}

void FakeXdsTransportFactory::FakeStreamingCall::CompleteSendMessageFromClient(
    bool ok) {
  GPR_ASSERT(!transport_->auto_complete_messages_from_client());
  MutexLock lock(&mu_);
  CompleteSendMessageFromClientLocked(ok);
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

FakeXdsTransportFactory::FakeXdsTransport::FakeXdsTransport(
    std::unique_ptr<ConnectivityStateReporter> connectivity_state_reporter,
    bool auto_complete_messages_from_client, bool auto_report_transport_ready)
    : auto_complete_messages_from_client_(auto_complete_messages_from_client),
      connectivity_state_reporter_(std::move(connectivity_state_reporter)) {
  if (auto_report_transport_ready) {
    // Send connectivity change update indicating the channel is connected.
    GetDefaultEventEngine()->Run(
        [connectivity_state_reporter = connectivity_state_reporter_]() mutable {
          ExecCtx exec_ctx;
          connectivity_state_reporter->ReportReady();
          connectivity_state_reporter.reset();
        });
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::ReportConnecting() {
  std::shared_ptr<ConnectivityStateReporter> connectivity_state_reporter;
  {
    MutexLock lock(&mu_);
    connectivity_state_reporter = connectivity_state_reporter_;
  }
  ExecCtx exec_ctx;
  if (connectivity_state_reporter != nullptr) {
    connectivity_state_reporter->ReportConnecting();
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::ReportReady() {
  std::shared_ptr<ConnectivityStateReporter> connectivity_state_reporter;
  {
    MutexLock lock(&mu_);
    connectivity_state_reporter = connectivity_state_reporter_;
  }
  ExecCtx exec_ctx;
  if (connectivity_state_reporter != nullptr) {
    connectivity_state_reporter->ReportReady();
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::ReportTransientFailure(
    absl::Status status) {
  std::shared_ptr<ConnectivityStateReporter> connectivity_state_reporter;
  {
    MutexLock lock(&mu_);
    connectivity_state_reporter = connectivity_state_reporter_;
  }
  ExecCtx exec_ctx;
  if (connectivity_state_reporter != nullptr) {
    connectivity_state_reporter->ReportTransientFailure(std::move(status));
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::Orphan() {
  {
    MutexLock lock(&mu_);
    // Can't destroy on_connectivity_change_ synchronously, since that
    // operation will trigger code in XdsClient that acquires its mutex, but
    // it was already holding its mutex when it called us, so it would deadlock.
    GetDefaultEventEngine()->Run([connectivity_state_reporter = std::move(
                                      connectivity_state_reporter_)]() mutable {
      ExecCtx exec_ctx;
      connectivity_state_reporter.reset();
    });
  }
  Unref();
}

RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall>
FakeXdsTransportFactory::FakeXdsTransport::WaitForStream(
    const char* method, absl::Duration timeout) {
  MutexLock lock(&mu_);
  auto it = active_calls_.find(method);
  while (it == active_calls_.end() || it->second == nullptr) {
    if (cv_.WaitWithTimeout(&mu_, timeout * grpc_test_slowdown_factor())) {
      return nullptr;
    }
    it = active_calls_.find(method);
  }
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
  auto call = MakeOrphanable<FakeStreamingCall>(Ref(), method,
                                                std::move(event_handler));
  MutexLock lock(&mu_);
  active_calls_[method] = call->Ref();
  cv_.Signal();
  return call;
}

//
// FakeXdsTransportFactory
//

constexpr char FakeXdsTransportFactory::kAdsMethod[];
constexpr char FakeXdsTransportFactory::kAdsV2Method[];

OrphanablePtr<XdsTransportFactory::XdsTransport>
FakeXdsTransportFactory::Create(
    const XdsBootstrap::XdsServer& server,
    std::unique_ptr<ConnectivityStateReporter> connectivity_state_reporter,
    absl::Status* /*status*/) {
  MutexLock lock(&mu_);
  auto& entry = transport_map_[&server];
  GPR_ASSERT(entry == nullptr);
  auto transport = MakeOrphanable<FakeXdsTransport>(
      std::move(connectivity_state_reporter),
      auto_complete_messages_from_client_, auto_report_transport_ready_);
  entry = transport->Ref();
  return transport;
}

void FakeXdsTransportFactory::ReportConnecting(
    const XdsBootstrap::XdsServer& server) {
  auto transport = GetTransport(server);
  transport->ReportConnecting();
}

void FakeXdsTransportFactory::ReportReady(
    const XdsBootstrap::XdsServer& server) {
  auto transport = GetTransport(server);
  transport->ReportReady();
}

void FakeXdsTransportFactory::ReportTransientFailure(
    const XdsBootstrap::XdsServer& server, absl::Status status) {
  auto transport = GetTransport(server);
  transport->ReportTransientFailure(std::move(status));
}

void FakeXdsTransportFactory::SetAutoCompleteMessagesFromClient(bool value) {
  MutexLock lock(&mu_);
  auto_complete_messages_from_client_ = value;
}

void FakeXdsTransportFactory::SetAutoReportTransportReady(bool value) {
  MutexLock lock(&mu_);
  auto_report_transport_ready_ = value;
}

RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall>
FakeXdsTransportFactory::WaitForStream(const XdsBootstrap::XdsServer& server,
                                       const char* method,
                                       absl::Duration timeout) {
  auto transport = GetTransport(server);
  return transport->WaitForStream(method, timeout);
}

RefCountedPtr<FakeXdsTransportFactory::FakeXdsTransport>
FakeXdsTransportFactory::GetTransport(const XdsBootstrap::XdsServer& server) {
  MutexLock lock(&mu_);
  RefCountedPtr<FakeXdsTransport> transport = transport_map_[&server];
  GPR_ASSERT(transport != nullptr);
  return transport;
}

}  // namespace grpc_core
