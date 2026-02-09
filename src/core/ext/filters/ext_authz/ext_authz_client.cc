//
// Copyright 2026 gRPC authors.
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

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"

#include <grpc/event_engine/event_engine.h>

#include <memory>
#include <optional>
#include <string>

#include "envoy/service/auth/v3/external_auth.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#define GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_XDS_RECONNECT_JITTER 0.2
#define GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS 1000

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

//
// Internal class declarations
//

// A call wrapper that can restart a call upon failure.
// The template parameter is the kind of wrapped call.
// TODO(roth): This is basically the same code as in XdsClient, and
// probably very similar to many other places in the codebase.
// Consider refactoring this into a common utility library somehow.
template <typename T>
class ExtAuthzClient::ExtAuthzChannel::RetryableCall final
    : public InternallyRefCounted<RetryableCall<T>> {
 public:
  explicit RetryableCall(WeakRefCountedPtr<ExtAuthzChannel> ext_authz_channel);

  // Disable thread-safety analysis because this method is called via
  // OrphanablePtr<>, but there's no way to pass the lock annotation
  // through there.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

  void OnCallFinishedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ExtAuthzClient::mu_);

  T* call() const { return call_.get(); }
  ExtAuthzChannel* ext_authz_channel() const {
    return ext_authz_channel_.get();
  }

  bool IsCurrentCallOnChannel() const;

 private:
  void StartNewCallLocked();
  void StartRetryTimerLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ExtAuthzClient::mu_);

  void OnRetryTimer();

  // The wrapped xds call that talks to the xds server. It's instantiated
  // every time we start a new call. It's null during call retry backoff.
  OrphanablePtr<T> call_;
  // The owning xds channel.
  WeakRefCountedPtr<ExtAuthzChannel> ext_authz_channel_;

  // Retry state.
  BackOff backoff_;
  std::optional<EventEngine::TaskHandle> timer_handle_
      ABSL_GUARDED_BY(&ExtAuthzClient::mu_);

  bool shutting_down_ = false;
};

//
// ExtAuthzClient::ExtAuthzChannel::RetryableCall<>
//

template <typename T>
ExtAuthzClient::ExtAuthzChannel::RetryableCall<T>::RetryableCall(
    WeakRefCountedPtr<ExtAuthzChannel> ext_authz_channel)
    : ext_authz_channel_(std::move(ext_authz_channel)),
      backoff_(BackOff::Options()
                   .set_initial_backoff(Duration::Seconds(
                       GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS))
                   .set_multiplier(GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER)
                   .set_jitter(GRPC_XDS_RECONNECT_JITTER)
                   .set_max_backoff(Duration::Seconds(
                       GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS))) {
  StartNewCallLocked();
}

template <typename T>
void ExtAuthzClient::ExtAuthzChannel::RetryableCall<T>::Orphan() {
  shutting_down_ = true;
  call_.reset();
  if (timer_handle_.has_value()) {
    ext_authz_channel()->ext_authz_client()->engine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  this->Unref(DEBUG_LOCATION, "RetryableCall+orphaned");
}

template <typename T>
void ExtAuthzClient::ExtAuthzChannel::RetryableCall<T>::OnCallFinishedLocked() {
  // If we saw a response on the current stream, reset backoff.
  if (call_->seen_response()) backoff_.Reset();
  call_.reset();
  // Start retry timer.
  StartRetryTimerLocked();
}

template <typename T>
void ExtAuthzClient::ExtAuthzChannel::RetryableCall<T>::StartNewCallLocked() {
  if (shutting_down_) return;
  GRPC_CHECK(ext_authz_channel_->transport_ != nullptr);
  GRPC_CHECK(call_ == nullptr);
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << ext_authz_channel()->ext_authz_client()
      << "] ext_authz server " << ext_authz_channel()->server_->server_uri()
      << ": start new call from retryable call " << this;
  call_ = MakeOrphanable<T>(
      this->Ref(DEBUG_LOCATION, "RetryableCall+start_new_call"));
}

template <typename T>
void ExtAuthzClient::ExtAuthzChannel::RetryableCall<
    T>::StartRetryTimerLocked() {
  if (shutting_down_) return;
  const Duration delay = backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << ext_authz_channel()->ext_authz_client()
      << "] ext_authz server " << ext_authz_channel()->server_->server_uri()
      << ": call attempt failed; retry timer will fire in " << delay.millis()
      << "ms.";
  timer_handle_ = ext_authz_channel()->ext_authz_client()->engine()->RunAfter(
      delay,
      [self = this->Ref(DEBUG_LOCATION, "RetryableCall+retry_timer_start")]() {
        ExecCtx exec_ctx;
        self->OnRetryTimer();
      });
}

template <typename T>
void ExtAuthzClient::ExtAuthzChannel::RetryableCall<T>::OnRetryTimer() {
  MutexLock lock(&ext_authz_channel_->ext_authz_client()->mu_);
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    if (shutting_down_) return;
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[ext_authz_client " << ext_authz_channel()->ext_authz_client()
        << "] ext_authz server " << ext_authz_channel()->server_->server_uri()
        << ": retry timer fired (retryable call: " << this << ")";
    StartNewCallLocked();
  }
}

// An ext_authz call to the external authorization server.
class ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall final
    : public InternallyRefCounted<ExtAuthzCall> {
 public:
  // The ctor and dtor should not be used directly.
  explicit ExtAuthzCall(
      RefCountedPtr<RetryableCall<ExtAuthzCall>> retryable_call);

  void Orphan() override;

  RetryableCall<ExtAuthzCall>* retryable_call() {
    return retryable_call_.get();
  }
  ExtAuthzChannel* ext_authz_channel() const {
    return retryable_call_->ext_authz_channel();
  }
  ExtAuthzClient* ext_authz_client() const {
    return ext_authz_channel()->ext_authz_client();
  }
  bool seen_response() const { return seen_response_; }

 private:
  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(RefCountedPtr<ExtAuthzCall> ext_authz_call)
        : ext_authz_call_(std::move(ext_authz_call)) {}

    void OnRequestSent(bool /*ok*/) override {
      ext_authz_call_->OnRequestSent();
    }
    void OnRecvMessage(absl::string_view payload) override {
      ext_authz_call_->OnRecvMessage(payload);
    }
    void OnStatusReceived(absl::Status status) override {
      ext_authz_call_->OnStatusReceived(std::move(status));
    }

   private:
    RefCountedPtr<ExtAuthzCall> ext_authz_call_;
  };

  void SendMessageLocked(std::string payload)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ExtAuthzClient::mu_);

  void OnRequestSent();
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  bool IsCurrentCallOnChannel() const;

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<ExtAuthzCall>> retryable_call_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
      streaming_call_;

  bool seen_response_ = false;
  bool send_message_pending_ ABSL_GUARDED_BY(&ExtAuthzClient::mu_) = false;
};

//
// ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall
//

ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall::ExtAuthzCall(
    RefCountedPtr<RetryableCall<ExtAuthzCall>> retryable_call)
    : InternallyRefCounted<ExtAuthzCall>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "ExtAuthzCall"
                                                       : nullptr),
      retryable_call_(std::move(retryable_call)) {
  GRPC_CHECK_NE(ext_authz_client(), nullptr);
  const char* method = "/envoy.service.auth.v3.Authorization/Check";
  streaming_call_ = ext_authz_channel()->transport_->CreateStreamingCall(
      method, std::make_unique<StreamEventHandler>(
                  // Passing the initial ref here.  This ref will go away when
                  // the StreamEventHandler is destroyed.
                  RefCountedPtr<ExtAuthzCall>(this)));
  GRPC_CHECK(streaming_call_ != nullptr);
  // Start the call.
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << ext_authz_client() << "] ext_authz server "
      << ext_authz_channel()->server_->server_uri()
      << ": starting ext_authz call (ext_authz_call=" << this
      << ", streaming_call=" << streaming_call_.get() << ")";
  // TODO(rishesh): add logic for sending request
  // Send the initial request.
  // std::string serialized_payload =
  // ext_authz_client()->GetOrCreateExtAuthzChannelLocked();
  // SendMessageLocked(std::move(serialized_payload));
  // Read initial response.
  streaming_call_->StartRecvMessage();
}

void ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall::Orphan() {
  // Note that the initial ref is held by the StreamEventHandler, which
  // will be destroyed when streaming_call_ is destroyed, which may not happen
  // here, since there may be other refs held to streaming_call_ by internal
  // callbacks.
  streaming_call_.reset();
}

void ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall::OnRequestSent() {
  MutexLock lock(&ext_authz_client()->mu_);
  send_message_pending_ = false;
}
void ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall::OnRecvMessage(
    absl::string_view payload) {
  MutexLock lock(&ext_authz_client()->mu_);
  // If we're no longer the current call, ignore the result.
  if (!IsCurrentCallOnChannel()) return;
  // Start recv after any code branch
  auto cleanup = absl::MakeCleanup(
      [call = streaming_call_.get()]() { call->StartRecvMessage(); });
  // Parse the response.
  seen_response_ = true;
}

void ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall::OnStatusReceived(
    absl::Status status) {
  MutexLock lock(&ext_authz_client()->mu_);
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << ext_authz_client() << "] ExtAuthz server "
      << ext_authz_channel()->server_->server_uri()
      << ": ExtAuthz call status received (ext_authz_channel="
      << ext_authz_channel() << ", ext_authz_call=" << this
      << ", streaming_call=" << streaming_call_.get() << "): " << status;
  // Ignore status from a stale call.
  if (IsCurrentCallOnChannel()) {
    // Try to restart the call.
    retryable_call_->OnCallFinishedLocked();
  }
}

bool ExtAuthzClient::ExtAuthzChannel::ExtAuthzCall::IsCurrentCallOnChannel()
    const {
  // If the retryable ExtAuthz call is null (which only happens when the
  // ExtAuthz channel is shutting down), all the ExtAuthz calls are stale.
  if (ext_authz_channel()->ext_authz_call_ == nullptr) return false;
  return this == ext_authz_channel()->ext_authz_call_->call();
}

//
// ExtAuthzClient::ExtAuthzChannel
//

ExtAuthzClient::ExtAuthzChannel::ExtAuthzChannel(
    WeakRefCountedPtr<ExtAuthzClient> ext_authz_client_,
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server)
    : DualRefCounted<ExtAuthzChannel>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "ExtAuthzChannel"
                                                       : nullptr),
      ext_authz_client_(std::move(ext_authz_client_)),
      server_(std::move(server)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << ext_authz_client_.get()
      << "] creating channel " << this << " for server "
      << server_->server_uri();
  absl::Status status;
  transport_ =
      ext_authz_client_->transport_factory_->GetTransport(*server_, &status);
  GRPC_CHECK(transport_ != nullptr);
  if (!status.ok()) {
    LOG(ERROR) << "Error creating ExtAuthz channel to " << server_->server_uri()
               << ": " << status;
  }
}

ExtAuthzClient::ExtAuthzChannel::~ExtAuthzChannel() {
  GRPC_TRACE_LOG(xds_client, INFO) << "[ext_authz_client " << ext_authz_client()
                                   << "] destroying ExtAuthz channel " << this
                                   << " for server " << server_->server_uri();
  ext_authz_client_.reset(DEBUG_LOCATION, "ExtAuthzChannel");
}

// This method should only ever be called when holding the lock, but we can't
// use a ABSL_EXCLUSIVE_LOCKS_REQUIRED annotation, because Orphan() will be
// called from DualRefCounted::Unref(), which cannot have a lock annotation for
// a lock in this subclass.
void ExtAuthzClient::ExtAuthzChannel::Orphaned()
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GRPC_TRACE_LOG(xds_client, INFO) << "[ext_authz_client " << ext_authz_client()
                                   << "] orphaning ExtAuthz channel " << this
                                   << " for server " << server_->server_uri();
  transport_.reset();
  // At this time, all strong refs are removed, remove from channel map to
  // prevent subsequent subscription from trying to use this ExtAuthzChannel as
  // it is shutting down.
  ext_authz_client_->ext_authz_channel_map_.erase(server_->Key());
  ext_authz_call_.reset();
}

void ExtAuthzClient::ExtAuthzChannel::ResetBackoff() {
  transport_->ResetBackoff();
}

void ExtAuthzClient::ExtAuthzChannel::StopExtAuthzCallLocked() {
  ext_authz_call_.reset();
}

//
// ExtAuthzClient
//

ExtAuthzClient::ExtAuthzClient(
    std::shared_ptr<XdsBootstrap> bootstrap,
    RefCountedPtr<XdsTransportFactory> transport_factory,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine)
    : DualRefCounted<ExtAuthzClient>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "ExtAuthzClient"
                                                       : nullptr),
      engine_(std::move(engine)),
      bootstrap_(std::move(bootstrap)),
      transport_factory_(std::move(transport_factory)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] creating ext_authz client";
}

ExtAuthzClient::~ExtAuthzClient() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] destroying ext_authz client";
}

RefCountedPtr<ExtAuthzClient::ExtAuthzChannel>
ExtAuthzClient::GetOrCreateExtAuthzChannelLocked(
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server,
    const char* reason) {
  std::string key = server->Key();
  auto it = ext_authz_channel_map_.find(key);
  if (it != ext_authz_channel_map_.end()) {
    return it->second->Ref(DEBUG_LOCATION, reason);
  }
  // Channel not found, so create a new one.
  auto ext_authz_channel = MakeRefCounted<ExtAuthzChannel>(
      WeakRef(DEBUG_LOCATION, "ExtAuthzChannel"), std::move(server));
  ext_authz_channel_map_[std::move(key)] = ext_authz_channel.get();
  return ext_authz_channel;
}

void ExtAuthzClient::ResetBackoff() {
  MutexLock lock(&mu_);
  for (auto& [_, ext_authz_channel] : ext_authz_channel_map_) {
    ext_authz_channel->ResetBackoff();
  }
}

}  // namespace grpc_core
