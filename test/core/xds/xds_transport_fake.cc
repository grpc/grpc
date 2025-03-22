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

#include "test/core/xds/xds_transport_fake.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {

//
// FakeXdsTransportFactory::FakeStreamingCall
//

FakeXdsTransportFactory::FakeStreamingCall::~FakeStreamingCall() {
  // Tests should not fail to read any messages from the client.
  {
    MutexLock lock(&mu_);
    if (transport_->abort_on_undrained_messages()) {
      for (const auto& message : from_client_messages_) {
        LOG(ERROR) << "[" << transport_->server()->server_uri() << "] " << this
                   << " From client message left in queue: " << message;
      }
      CHECK(from_client_messages_.empty());
    }
  }
  // Can't call event_handler_->OnStatusReceived() or unref event_handler_
  // synchronously, since those operations will trigger code in
  // XdsClient that acquires its mutex, but it was already holding its
  // mutex when it called us, so it would deadlock.
  event_engine_->Run([event_handler = std::move(event_handler_),
                      status_sent = status_sent_]() mutable {
    ExecCtx exec_ctx;
    if (!status_sent) event_handler->OnStatusReceived(absl::OkStatus());
    event_handler.reset();
  });
}

void FakeXdsTransportFactory::FakeStreamingCall::Orphan() {
  {
    MutexLock lock(&mu_);
    orphaned_ = true;
  }
  transport_->RemoveStream(method_, this);
  Unref();
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessage(
    std::string payload) {
  MutexLock lock(&mu_);
  CHECK(!orphaned_);
  from_client_messages_.push_back(std::move(payload));
  if (transport_->auto_complete_messages_from_client()) {
    CompleteSendMessageFromClientLocked(/*ok=*/true);
  }
}

bool FakeXdsTransportFactory::FakeStreamingCall::HaveMessageFromClient() {
  MutexLock lock(&mu_);
  return !from_client_messages_.empty();
}

std::optional<std::string>
FakeXdsTransportFactory::FakeStreamingCall::WaitForMessageFromClient() {
  while (true) {
    {
      MutexLock lock(&mu_);
      if (!from_client_messages_.empty()) {
        std::string payload = std::move(from_client_messages_.front());
        from_client_messages_.pop_front();
        return payload;
      }
      if (event_engine_->IsIdle()) return std::nullopt;
    }
    event_engine_->Tick();
  }
}

void FakeXdsTransportFactory::FakeStreamingCall::
    CompleteSendMessageFromClientLocked(bool ok) {
  // Can't call event_handler_->OnRequestSent() synchronously, since that
  // operation will trigger code in XdsClient that acquires its mutex, but it
  // was already holding its mutex when it called us, so it would deadlock.
  event_engine_->Run([event_handler = event_handler_->Ref(), ok]() mutable {
    ExecCtx exec_ctx;
    event_handler->OnRequestSent(ok);
    event_handler.reset();
  });
}

void FakeXdsTransportFactory::FakeStreamingCall::CompleteSendMessageFromClient(
    bool ok) {
  CHECK(!transport_->auto_complete_messages_from_client());
  MutexLock lock(&mu_);
  CompleteSendMessageFromClientLocked(ok);
}

void FakeXdsTransportFactory::FakeStreamingCall::StartRecvMessage() {
  MutexLock lock(&mu_);
  if (num_pending_reads_ > 0) {
    transport_->factory()->too_many_pending_reads_callback_();
  }
  ++reads_started_;
  ++num_pending_reads_;
  if (!to_client_messages_.empty()) {
    // Dispatch pending message (if there's one) on a separate thread to avoid
    // recursion
    event_engine_->Run([call = RefAsSubclass<FakeStreamingCall>()]() {
      call->MaybeDeliverMessageToClient();
    });
  }
}

void FakeXdsTransportFactory::FakeStreamingCall::SendMessageToClient(
    absl::string_view payload) {
  {
    MutexLock lock(&mu_);
    to_client_messages_.emplace_back(payload);
  }
  MaybeDeliverMessageToClient();
}

void FakeXdsTransportFactory::FakeStreamingCall::MaybeDeliverMessageToClient() {
  RefCountedPtr<RefCountedEventHandler> event_handler;
  std::string message;
  // Loop terminates with a break inside
  while (true) {
    {
      MutexLock lock(&mu_);
      if (num_pending_reads_ == 0 || to_client_messages_.empty()) {
        break;
      }
      --num_pending_reads_;
      message = std::move(to_client_messages_.front());
      to_client_messages_.pop_front();
      event_handler = event_handler_;
    }
    ExecCtx exec_ctx;
    event_handler->OnRecvMessage(message);
  }
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

bool FakeXdsTransportFactory::FakeStreamingCall::WaitForReadsStarted(
    size_t expected) {
  while (true) {
    {
      MutexLock lock(&mu_);
      if (reads_started_ == expected) return true;
      if (event_engine_->IsIdle()) return false;
    }
    event_engine_->Tick();
  }
}

bool FakeXdsTransportFactory::FakeStreamingCall::IsOrphaned() {
  MutexLock lock(&mu_);
  return orphaned_;
}

//
// FakeXdsTransportFactory::FakeXdsTransport
//

void FakeXdsTransportFactory::FakeXdsTransport::TriggerConnectionFailure(
    absl::Status status) {
  std::set<RefCountedPtr<ConnectivityFailureWatcher>> watchers;
  {
    MutexLock lock(&mu_);
    watchers = watchers_;
  }
  ExecCtx exec_ctx;
  for (const auto& watcher : watchers) {
    watcher->OnConnectivityFailure(status);
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::Orphaned() {
  {
    MutexLock lock(&factory_->mu_);
    auto it = factory_->transport_map_.find(server_.Key());
    if (it != factory_->transport_map_.end() && it->second == this) {
      factory_->transport_map_.erase(it);
    }
  }
  factory_.reset();
  {
    MutexLock lock(&mu_);
    // Can't destroy watchers synchronously, since that operation will trigger
    // code in XdsClient that acquires its mutex, but it was already holding
    // its mutex when it called us, so it would deadlock.
    event_engine_->Run([watchers = std::move(watchers_)]() mutable {
      ExecCtx exec_ctx;
      watchers.clear();
    });
  }
}

RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall>
FakeXdsTransportFactory::FakeXdsTransport::WaitForStream(const char* method) {
  while (true) {
    {
      MutexLock lock(&mu_);
      auto it = active_calls_.find(method);
      if (it != active_calls_.end() && it->second != nullptr) return it->second;
      if (event_engine_->IsIdle()) return nullptr;
    }
    event_engine_->Tick();
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::RemoveStream(
    const char* method, FakeStreamingCall* call) {
  MutexLock lock(&mu_);
  auto it = active_calls_.find(method);
  if (it != active_calls_.end() && it->second.get() == call) {
    active_calls_.erase(it);
  }
}

void FakeXdsTransportFactory::FakeXdsTransport::StartConnectivityFailureWatch(
    RefCountedPtr<ConnectivityFailureWatcher> watcher) {
  MutexLock lock(&mu_);
  watchers_.insert(std::move(watcher));
}

void FakeXdsTransportFactory::FakeXdsTransport::StopConnectivityFailureWatch(
    const RefCountedPtr<ConnectivityFailureWatcher>& watcher) {
  MutexLock lock(&mu_);
  watchers_.erase(watcher);
}

OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
FakeXdsTransportFactory::FakeXdsTransport::CreateStreamingCall(
    const char* method,
    std::unique_ptr<StreamingCall::EventHandler> event_handler) {
  auto call = MakeOrphanable<FakeStreamingCall>(
      WeakRefAsSubclass<FakeXdsTransport>(), method, std::move(event_handler));
  MutexLock lock(&mu_);
  active_calls_[method] = call->Ref().TakeAsSubclass<FakeStreamingCall>();
  return call;
}

//
// FakeXdsTransportFactory
//

constexpr char FakeXdsTransportFactory::kAdsMethod[];
constexpr char FakeXdsTransportFactory::kLrsMethod[];

RefCountedPtr<XdsTransportFactory::XdsTransport>
FakeXdsTransportFactory::GetTransport(
    const XdsBootstrap::XdsServerTarget& server, absl::Status* /*status*/) {
  std::string key = server.Key();
  MutexLock lock(&mu_);
  auto transport = GetTransportLocked(key);
  if (transport == nullptr) {
    transport = MakeRefCounted<FakeXdsTransport>(
        WeakRefAsSubclass<FakeXdsTransportFactory>(), server,
        auto_complete_messages_from_client_, abort_on_undrained_messages_);
    transport_map_.emplace(std::move(key), transport.get());
  }
  return transport;
}

void FakeXdsTransportFactory::TriggerConnectionFailure(
    const XdsBootstrap::XdsServerTarget& server, absl::Status status) {
  auto transport = GetTransport(server);
  if (transport == nullptr) return;
  transport->TriggerConnectionFailure(std::move(status));
}

void FakeXdsTransportFactory::SetAutoCompleteMessagesFromClient(bool value) {
  MutexLock lock(&mu_);
  auto_complete_messages_from_client_ = value;
}

void FakeXdsTransportFactory::SetAbortOnUndrainedMessages(bool value) {
  MutexLock lock(&mu_);
  abort_on_undrained_messages_ = value;
}

RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall>
FakeXdsTransportFactory::WaitForStream(
    const XdsBootstrap::XdsServerTarget& server, const char* method) {
  auto transport = GetTransport(server);
  if (transport == nullptr) return nullptr;
  return transport->WaitForStream(method);
}

void FakeXdsTransportFactory::Orphaned() { event_engine_.reset(); }

RefCountedPtr<FakeXdsTransportFactory::FakeXdsTransport>
FakeXdsTransportFactory::GetTransport(
    const XdsBootstrap::XdsServerTarget& server) {
  std::string key = server.Key();
  MutexLock lock(&mu_);
  return GetTransportLocked(key);
}

RefCountedPtr<FakeXdsTransportFactory::FakeXdsTransport>
FakeXdsTransportFactory::GetTransportLocked(const std::string& key) {
  auto it = transport_map_.find(key);
  if (it == transport_map_.end()) return nullptr;
  return it->second->RefIfNonZero().TakeAsSubclass<FakeXdsTransport>();
}

}  // namespace grpc_core
