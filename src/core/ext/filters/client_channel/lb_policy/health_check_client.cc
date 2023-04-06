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

#include "src/core/ext/filters/client_channel/lb_policy/health_check_client_internal.h"

#include "upb/upb.hpp"

#include <grpc/status.h>

#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_interface_internal.h"
#include "src/core/ext/filters/client_channel/subchannel_stream_client.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/proto/grpc/health/v1/health.upb.h"

namespace grpc_core {

// FIXME
//TraceFlag grpc_health_check_client_trace(false, "health_check_client");
extern TraceFlag grpc_health_check_client_trace;

namespace {

// A fire-and-forget class to asynchronously drain a WorkSerializer queue.
class AsyncWorkSerializerDrainer {
 public:
  explicit AsyncWorkSerializerDrainer(
      std::shared_ptr<WorkSerializer> work_serializer)
      : work_serializer_(std::move(work_serializer)) {
    GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
    ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
  }

 private:
  static void RunInExecCtx(void* arg, grpc_error_handle) {
    auto* self = static_cast<AsyncWorkSerializerDrainer*>(arg);
    self->work_serializer_.DrainQueue();
    delete self;
  }

  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_closure closure_;
};

}  // namespace

//
// HealthProducer::HealthChecker
//

class HealthProducer::HealthChecker
    : public InternallyRefCounted<HealthChecker> {
 public:
  HealthChecker(WeakRefCountedPtr<HealthProducer> producer,
                absl::string_view health_check_service_name)
      : producer_(std::move(producer)),
        health_check_service_name_(health_check_service_name),
        state_(producer_->state == GRPC_CHANNEL_READY
                   ? GRPC_CHANNEL_CONNECTING
                   : producer_->state_),
        status_(producer_->status) {
    // If the subchannel is already connected, start health checking.
    if (producer_->state_ == GRPC_CHANNEL_READY) StartHealthStreamLocked();
  }

  // Disable thread-safety analysis because this method is called via
  // OrphanablePtr<>, but there's no way to pass the lock annotation
  // through there.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS {
    stream_client_.reset();
    Unref();
  }

  void AddWatcherLocked(HealthWatcher* watcher)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&HealthProducer::mu_) {
    watchers_.insert(watcher);
    watcher->Notify(state_, status_);
  }

  // Returns true if this was the last watcher.
  bool RemoveWatcherLocked(HealthWatcher* watcher)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&HealthProducer::mu_) {
    watchers_.erase(watcher);
    return watchers_.empty();
  }

  void OnConnectivityStateChangeLocked(grpc_connectivity_state state,
                                       const absl::Status& status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&HealthProducer::mu_) {
    if (state_ == GRPC_CHANNEL_READY) {
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

 private:
  class HealthStreamEventHandler;

  // Starts a new stream if we have a connected subchannel.
  // Called whenever the subchannel transitions to state READY or when a
  // watcher is added.
  void StartHealthStreamLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&HealthProducer::mu_) {
    stream_client_ = MakeOrphanable<SubchannelStreamClient>(
        producer_->connected_subchannel_, producer_->subchannel_->pollset_set(),
        absl::make_unique<HealthStreamEventHandler>(Ref()),
        GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)
            ? "HealthClient"
            : nullptr);
  }

  // Notifies watchers of a new state.
  // Called while holding the SubchannelStreamClient lock and possibly
  // the producer lock, so must notify asynchronously, but in guaranteed
  // order (hence the use of WorkSerializer).
  void NotifyWatchersLocked(grpc_connectivity_state state,
                            absl::Status status) {
// FIXME: fix trace messages
    if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
      gpr_log(GPR_INFO, "HealthProducer %p: reporting state %s to watchers",
              this, ConnectivityStateName(state));
    }
    work_serializer_->Schedule(
        [self = Ref(), state, status = std::move(status)]() mutable {
          MutexLock lock(&self->producer_->mu_);
          for (HealthWatcher* watcher : self->watchers_) {
            watcher->Notify(state, std::move(status));
          }
        },
        DEBUG_LOCATION);
    new AsyncWorkSerializerDrainer(work_serializer_);
  }

  void OnHealthWatchStatusChange(grpc_connectivity_state state,
                                 const absl::Status& status) {
    MutexLock lock(&producer_->mu_);
    if (state != GRPC_CHANNEL_SHUTDOWN && stream_client_ != nullptr) {
      state_ = state;
      status_ = status;
      NotifyWatchersLocked(state, status);
    }
  }

  WeakRefCountedPtr<HealthProducer> producer_;
  absl::string_view health_check_service_name_;
  std::shared_ptr<WorkSerializer> work_serializer_ =
      std::make_shared<WorkSerializer>();

  grpc_connectivity_state state_ ABSL_GUARDED_BY(&HealthProducer::mu_);
  absl::Status status_ ABSL_GUARDED_BY(&HealthProducer::mu_);
  OrphanablePtr<SubchannelStreamClient> stream_client_
      ABSL_GUARDED_BY(&HealthProducer::mu_);
  std::set<HealthWatcher*> watchers_ ABSL_GUARDED_BY(&HealthProducer::mu_);
};

//
// HealthProducer::HealthChecker::HealthStreamEventHandler
//

class HealthProducer::HealthChecker::HealthStreamEventHandler
    : public SubchannelStreamClient::CallEventHandler {
 public:
  explicit HealthStreamEventHandler(
      RefCountedPtr<HealthChecker> health_checker)
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
        request_struct, upb_StringView_FromDataAndSize(
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
        state,
        state == GRPC_CHANNEL_TRANSIENT_FAILURE
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
      : producer_(std::move(producer)),
        interested_parties_(grpc_pollset_set_create()) {}

  ~ConnectivityWatcher() override {
    grpc_pollset_set_destroy(interested_parties_);
  }

  void OnConnectivityStateChange(grpc_connectivity_state state,
                                 const absl::Status&) override {
    producer_->OnConnectivityStateChange(state);
  }

  grpc_pollset_set* interested_parties() override {
    return interested_parties_;
  }

 private:
  WeakRefCountedPtr<HealthProducer> producer_;
  grpc_pollset_set* interested_parties_;
};

//
// HealthProducer
//

void HealthProducer::Start(RefCountedPtr<Subchannel> subchannel) {
  subchannel_ = std::move(subchannel);
  connected_subchannel_ = subchannel_->connected_subchannel();
  auto connectivity_watcher = MakeRefCounted<ConnectivityWatcher>(WeakRef());
  connectivity_watcher_ = connectivity_watcher.get();
  subchannel_->WatchConnectivityState(
      /*health_check_service_name=*/absl::nullopt,
      std::move(connectivity_watcher));
}

void HealthProducer::Orphan() {
  {
    MutexLock lock(&mu_);
    health_checkers_.clear();
  }
  subchannel_->CancelConnectivityStateWatch(
      /*health_check_service_name=*/absl::nullopt, connectivity_watcher_);
  subchannel_->RemoveDataProducer(this);
}

void HealthProducer::AddWatcher(HealthWatcher* watcher,
                                const std::string& health_check_service_name) {
  MutexLock lock(&mu_);
  auto it = health_checkers_.emplace(health_check_service_name, nullptr).first;
  auto& health_checker = it->second;
  if (health_checker == nullptr) {
    health_checker = MakeOrphanable<HealthChecker>(WeakRef(), it->first);
  }
  health_checker->AddWatcherLocked(watcher);
}

void HealthProducer::RemoveWatcher(
    HealthWatcher* watcher, const std::string& health_check_service_name) {
  MutexLock lock(&mu_);
  auto it = health_checkers_.find(health_check_service_name);
  if (it == health_checkers_.end()) return;
  const bool empty = it->second->RemoveWatcherLocked(watcher);
  if (empty) health_checkers_.erase(it);
}

void HealthProducer::OnConnectivityStateChange(grpc_connectivity_state state) {
  MutexLock lock(&mu_);
  if (state == GRPC_CHANNEL_READY) {
    connected_subchannel_ = subchannel_->connected_subchannel();
    for (const auto& p : health_checkers_) {
      p.second->MaybeStartStreamLocked();
    }
  } else {
    connected_subchannel_.reset();
    for (const auto& p : health_checkers_) {
      p.second->MaybeStopStreamLocked();
    }
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
  work_serializer_->Schedule(
      [watcher = watcher_, state, status = std::move(status)]() mutable {
        watcher->OnConnectivityStateChange(state, std::move(status));
      },
      DEBUG_LOCATION);
  new AsyncWorkSerializerDrainer(work_serializer_);
}

//
// External API
//

std::unique_ptr<SubchannelInterface::DataWatcherInterface>
MakeHealthCheckWatcher(
    std::shared_ptr<WorkSerializer> work_serializer,
    absl::string_view health_check_service_name,
    std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
        watcher) {
  return std::make_unique<HealthWatcher>(
      std::move(work_serializer), health_check_service_name,
      std::move(watcher));
}

}  // namespace grpc_core
