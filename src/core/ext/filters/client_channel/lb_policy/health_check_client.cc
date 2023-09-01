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

#include <stdint.h>
#include <string.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "upb/base/string_view.h"
#include "upb/upb.hpp"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/health_check_client_internal.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_stream_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/proto/grpc/health/v1/health.upb.h"

namespace grpc_core {

TraceFlag grpc_health_check_client_trace(false, "health_check_client");

//
// HealthProducer::HealthChecker
//

HealthProducer::HealthChecker::HealthChecker(
    WeakRefCountedPtr<HealthProducer> producer,
    absl::string_view health_check_service_name)
    : producer_(std::move(producer)),
      health_check_service_name_(health_check_service_name),
      state_(producer_->state_ == GRPC_CHANNEL_READY ? GRPC_CHANNEL_CONNECTING
                                                     : producer_->state_),
      status_(producer_->status_) {
  // If the subchannel is already connected, start health checking.
  if (producer_->state_ == GRPC_CHANNEL_READY) StartHealthStreamLocked();
}

void HealthProducer::HealthChecker::Orphan() {
  stream_client_.reset();
  Unref();
}

void HealthProducer::HealthChecker::AddWatcherLocked(HealthWatcher* watcher) {
  watchers_.insert(watcher);
  watcher->Notify(state_, status_);
}

bool HealthProducer::HealthChecker::RemoveWatcherLocked(
    HealthWatcher* watcher) {
  watchers_.erase(watcher);
  return watchers_.empty();
}

void HealthProducer::HealthChecker::OnConnectivityStateChangeLocked(
    grpc_connectivity_state state, const absl::Status& status) {
  if (state == GRPC_CHANNEL_READY) {
    // We should already be in CONNECTING, and we don't want to change
    // that until we see the initial response on the stream.
    GPR_ASSERT(state_ == GRPC_CHANNEL_CONNECTING);
    // Start the health watch stream.
    StartHealthStreamLocked();
  } else {
    state_ = state;
    status_ = status;
    NotifyWatchersLocked(state_, status_);
    // We're not connected, so stop health checking.
    stream_client_.reset();
  }
}

void HealthProducer::HealthChecker::StartHealthStreamLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO,
            "HealthProducer %p HealthChecker %p: "
            "creating HealthClient for \"%s\"",
            producer_.get(), this,
            std::string(health_check_service_name_).c_str());
  }
  stream_client_ = MakeOrphanable<SubchannelStreamClient>(
      producer_->connected_subchannel_, producer_->subchannel_->pollset_set(),
      std::make_unique<HealthStreamEventHandler>(Ref()),
      GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace) ? "HealthClient"
                                                              : nullptr);
}

void HealthProducer::HealthChecker::NotifyWatchersLocked(
    grpc_connectivity_state state, absl::Status status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(
        GPR_INFO,
        "HealthProducer %p HealthChecker %p: reporting state %s to watchers",
        producer_.get(), this, ConnectivityStateName(state));
  }
  work_serializer_->Run([self = Ref(), state, status = std::move(status)]() {
    MutexLock lock(&self->producer_->mu_);
    for (HealthWatcher* watcher : self->watchers_) {
      watcher->Notify(state, status);
    }
  });
}

void HealthProducer::HealthChecker::OnHealthWatchStatusChange(
    grpc_connectivity_state state, const absl::Status& status) {
  if (state == GRPC_CHANNEL_SHUTDOWN) return;
  work_serializer_->Run([self = Ref(), state, status]() {
    MutexLock lock(&self->producer_->mu_);
    if (self->stream_client_ != nullptr) {
      self->state_ = state;
      self->status_ = status;
      for (HealthWatcher* watcher : self->watchers_) {
        watcher->Notify(state, self->status_);
      }
    }
  });
}

//
// HealthProducer::HealthChecker::HealthStreamEventHandler
//

class HealthProducer::HealthChecker::HealthStreamEventHandler
    : public SubchannelStreamClient::CallEventHandler {
 public:
  explicit HealthStreamEventHandler(RefCountedPtr<HealthChecker> health_checker)
      : health_checker_(std::move(health_checker)) {}

  Slice GetPathLocked() override {
    return Slice::FromStaticString("/grpc.health.v1.Health/Watch");
  }

  void OnCallStartLocked(SubchannelStreamClient* client) override {
    SetHealthStatusLocked(client, GRPC_CHANNEL_CONNECTING,
                          "starting health watch");
  }

  void OnRetryTimerStartLocked(SubchannelStreamClient* client) override {
    SetHealthStatusLocked(client, GRPC_CHANNEL_TRANSIENT_FAILURE,
                          "health check call failed; will retry after backoff");
  }

  grpc_slice EncodeSendMessageLocked() override {
    upb::Arena arena;
    grpc_health_v1_HealthCheckRequest* request_struct =
        grpc_health_v1_HealthCheckRequest_new(arena.ptr());
    grpc_health_v1_HealthCheckRequest_set_service(
        request_struct,
        upb_StringView_FromDataAndSize(
            health_checker_->health_check_service_name_.data(),
            health_checker_->health_check_service_name_.size()));
    size_t buf_length;
    char* buf = grpc_health_v1_HealthCheckRequest_serialize(
        request_struct, arena.ptr(), &buf_length);
    grpc_slice request_slice = GRPC_SLICE_MALLOC(buf_length);
    memcpy(GRPC_SLICE_START_PTR(request_slice), buf, buf_length);
    return request_slice;
  }

  absl::Status RecvMessageReadyLocked(
      SubchannelStreamClient* client,
      absl::string_view serialized_message) override {
    auto healthy = DecodeResponse(serialized_message);
    if (!healthy.ok()) {
      SetHealthStatusLocked(client, GRPC_CHANNEL_TRANSIENT_FAILURE,
                            healthy.status().ToString().c_str());
      return healthy.status();
    }
    if (!*healthy) {
      SetHealthStatusLocked(client, GRPC_CHANNEL_TRANSIENT_FAILURE,
                            "backend unhealthy");
    } else {
      SetHealthStatusLocked(client, GRPC_CHANNEL_READY, "OK");
    }
    return absl::OkStatus();
  }

  void RecvTrailingMetadataReadyLocked(SubchannelStreamClient* client,
                                       grpc_status_code status) override {
    if (status == GRPC_STATUS_UNIMPLEMENTED) {
      static const char kErrorMessage[] =
          "health checking Watch method returned UNIMPLEMENTED; "
          "disabling health checks but assuming server is healthy";
      gpr_log(GPR_ERROR, kErrorMessage);
      auto* channelz_node =
          health_checker_->producer_->subchannel_->channelz_node();
      if (channelz_node != nullptr) {
        channelz_node->AddTraceEvent(
            channelz::ChannelTrace::Error,
            grpc_slice_from_static_string(kErrorMessage));
      }
      SetHealthStatusLocked(client, GRPC_CHANNEL_READY, kErrorMessage);
    }
  }

 private:
  // Returns true if healthy.
  static absl::StatusOr<bool> DecodeResponse(
      absl::string_view serialized_message) {
    // Deserialize message.
    upb::Arena arena;
    auto* response = grpc_health_v1_HealthCheckResponse_parse(
        serialized_message.data(), serialized_message.size(), arena.ptr());
    if (response == nullptr) {
      // Can't parse message; assume unhealthy.
      return absl::InvalidArgumentError("cannot parse health check response");
    }
    int32_t status = grpc_health_v1_HealthCheckResponse_status(response);
    return status == grpc_health_v1_HealthCheckResponse_SERVING;
  }

  void SetHealthStatusLocked(SubchannelStreamClient* client,
                             grpc_connectivity_state state,
                             const char* reason) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
      gpr_log(GPR_INFO, "HealthCheckClient %p: setting state=%s reason=%s",
              client, ConnectivityStateName(state), reason);
    }
    health_checker_->OnHealthWatchStatusChange(
        state, state == GRPC_CHANNEL_TRANSIENT_FAILURE
                   ? absl::UnavailableError(reason)
                   : absl::OkStatus());
  }

  RefCountedPtr<HealthChecker> health_checker_;
};

//
// HealthProducer::ConnectivityWatcher
//

class HealthProducer::ConnectivityWatcher
    : public Subchannel::ConnectivityStateWatcherInterface {
 public:
  explicit ConnectivityWatcher(WeakRefCountedPtr<HealthProducer> producer)
      : producer_(std::move(producer)) {}

  void OnConnectivityStateChange(
      RefCountedPtr<ConnectivityStateWatcherInterface> self,
      grpc_connectivity_state state, const absl::Status& status) override {
    producer_->OnConnectivityStateChange(state, status);
    self.reset();
  }

  grpc_pollset_set* interested_parties() override {
    return producer_->interested_parties_;
  }

 private:
  WeakRefCountedPtr<HealthProducer> producer_;
};

//
// HealthProducer
//

void HealthProducer::Start(RefCountedPtr<Subchannel> subchannel) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthProducer %p: starting with subchannel %p", this,
            subchannel.get());
  }
  subchannel_ = std::move(subchannel);
  {
    MutexLock lock(&mu_);
    connected_subchannel_ = subchannel_->connected_subchannel();
  }
  auto connectivity_watcher = MakeRefCounted<ConnectivityWatcher>(WeakRef());
  connectivity_watcher_ = connectivity_watcher.get();
  subchannel_->WatchConnectivityState(std::move(connectivity_watcher));
}

void HealthProducer::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthProducer %p: shutting down", this);
  }
  {
    MutexLock lock(&mu_);
    health_checkers_.clear();
  }
  subchannel_->CancelConnectivityStateWatch(connectivity_watcher_);
  subchannel_->RemoveDataProducer(this);
}

void HealthProducer::AddWatcher(
    HealthWatcher* watcher,
    const absl::optional<std::string>& health_check_service_name) {
  MutexLock lock(&mu_);
  grpc_pollset_set_add_pollset_set(interested_parties_,
                                   watcher->interested_parties());
  if (!health_check_service_name.has_value()) {
    watcher->Notify(state_, status_);
    non_health_watchers_.insert(watcher);
  } else {
    auto it =
        health_checkers_.emplace(*health_check_service_name, nullptr).first;
    auto& health_checker = it->second;
    if (health_checker == nullptr) {
      health_checker = MakeOrphanable<HealthChecker>(WeakRef(), it->first);
    }
    health_checker->AddWatcherLocked(watcher);
  }
}

void HealthProducer::RemoveWatcher(
    HealthWatcher* watcher,
    const absl::optional<std::string>& health_check_service_name) {
  MutexLock lock(&mu_);
  grpc_pollset_set_del_pollset_set(interested_parties_,
                                   watcher->interested_parties());
  if (!health_check_service_name.has_value()) {
    non_health_watchers_.erase(watcher);
  } else {
    auto it = health_checkers_.find(*health_check_service_name);
    if (it == health_checkers_.end()) return;
    const bool empty = it->second->RemoveWatcherLocked(watcher);
    if (empty) health_checkers_.erase(it);
  }
}

void HealthProducer::OnConnectivityStateChange(grpc_connectivity_state state,
                                               const absl::Status& status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO,
            "HealthProducer %p: subchannel state update: state=%s status=%s",
            this, ConnectivityStateName(state), status.ToString().c_str());
  }
  MutexLock lock(&mu_);
  state_ = state;
  status_ = status;
  if (state == GRPC_CHANNEL_READY) {
    connected_subchannel_ = subchannel_->connected_subchannel();
  } else {
    connected_subchannel_.reset();
  }
  for (const auto& p : health_checkers_) {
    p.second->OnConnectivityStateChangeLocked(state, status);
  }
  for (HealthWatcher* watcher : non_health_watchers_) {
    watcher->Notify(state, status);
  }
}

//
// HealthWatcher
//

HealthWatcher::~HealthWatcher() {
  if (producer_ != nullptr) {
    producer_->RemoveWatcher(this, health_check_service_name_);
  }
}

void HealthWatcher::SetSubchannel(Subchannel* subchannel) {
  bool created = false;
  // Check if our producer is already registered with the subchannel.
  // If not, create a new one.
  subchannel->GetOrAddDataProducer(
      HealthProducer::Type(),
      [&](Subchannel::DataProducerInterface** producer) {
        if (*producer != nullptr) producer_ = (*producer)->RefIfNonZero();
        if (producer_ == nullptr) {
          producer_ = MakeRefCounted<HealthProducer>();
          *producer = producer_.get();
          created = true;
        }
      });
  // If we just created the producer, start it.
  // This needs to be done outside of the lambda passed to
  // GetOrAddDataProducer() to avoid deadlocking by re-acquiring the
  // subchannel lock while already holding it.
  if (created) producer_->Start(subchannel->Ref());
  // Register ourself with the producer.
  producer_->AddWatcher(this, health_check_service_name_);
}

void HealthWatcher::Notify(grpc_connectivity_state state, absl::Status status) {
  work_serializer_->Run(
      [watcher = watcher_, state, status = std::move(status)]() mutable {
        watcher->OnConnectivityStateChange(state, std::move(status));
      });
}

//
// External API
//

std::unique_ptr<SubchannelInterface::DataWatcherInterface>
MakeHealthCheckWatcher(
    std::shared_ptr<WorkSerializer> work_serializer, const ChannelArgs& args,
    std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
        watcher) {
  absl::optional<std::string> health_check_service_name;
  if (!args.GetBool(GRPC_ARG_INHIBIT_HEALTH_CHECKING).value_or(false)) {
    health_check_service_name =
        args.GetOwnedString(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME);
  }
  return std::make_unique<HealthWatcher>(std::move(work_serializer),
                                         std::move(health_check_service_name),
                                         std::move(watcher));
}

}  // namespace grpc_core
