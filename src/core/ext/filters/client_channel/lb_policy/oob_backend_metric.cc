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

#include "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h"

#include <grpc/status.h>

#include "upb/upb.hpp"

#include "src/core/ext/filters/client_channel/backend_metric.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_interface_internal.h"
#include "src/core/ext/filters/client_channel/subchannel_stream_client.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"
#include "xds/data/orca/v3/orca_load_report.upb.h"
#include "xds/service/orca/v3/orca.upb.h"

namespace grpc_core {

namespace {

TraceFlag grpc_orca_client_trace(false, "orca_client");

constexpr char kProducerType[] = "orca";

class OrcaWatcher;

// This producer is registered with a subchannel.  It creates a
// streaming ORCA call and reports the resulting backend metrics to all
// registered watchers.
class OrcaProducer : public Subchannel::DataProducerInterface {
 public:
  OrcaProducer(RefCountedPtr<Subchannel> subchannel)
      : subchannel_(std::move(subchannel)) {
    subchannel_->AddDataProducer(this);
  }

  void Orphan() override {
    subchannel_->RemoveDataProducer(this);
    stream_client_.reset();
  }

  const char* type() override { return kProducerType; }

  void OnConnected(RefCountedPtr<ConnectedSubchannel> connected_subchannel)
      override;

  void OnDisconnected() override;

  void AddWatcher(WeakRefCountedPtr<OrcaWatcher> watcher);

  void RemoveWatcher(OrcaWatcher* watcher);

 private:
  class OrcaStreamEventHandler
      : public SubchannelStreamClient::CallEventHandler {
   public:
    explicit OrcaStreamEventHandler(WeakRefCountedPtr<OrcaProducer> producer)
        : producer_(std::move(producer)) {}

    Slice GetPathLocked() override {
      return Slice::FromStaticString(
          "/xds.service.orca.v3.OpenRcaService/StreamCoreMetrics");
    }

    void OnCallStartLocked(SubchannelStreamClient* client) override {}

    void OnRetryTimerStartLocked(SubchannelStreamClient* client) override {}

    grpc_slice EncodeSendMessageLocked() override;

    void RecvMessageReadyLocked(SubchannelStreamClient* client, char* message,
                                size_t size) override;

    void RecvTrailingMetadataReadyLocked(SubchannelStreamClient* client,
                                         grpc_status_code status) override;

   private:
    // Returns true if healthy.
    absl::StatusOr<bool> DecodeResponse(char* message, size_t size);

    void SetHealthStatusLocked(SubchannelStreamClient* client,
                               grpc_connectivity_state state,
                               const char* reason);

    WeakRefCountedPtr<OrcaProducer> producer_;
  };

  Duration GetMinIntervalLocked() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  void StartStreamLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  RefCountedPtr<Subchannel> subchannel_;
  Mutex mu_;
  // TODO(roth): Use std::set<> instead once we can use C++14 heterogenous
  // map lookups.
  std::map<OrcaWatcher*, WeakRefCountedPtr<OrcaWatcher>> watcher_map_
      ABSL_GUARDED_BY(mu_);
  Duration report_interval_ ABSL_GUARDED_BY(mu_) = Duration::Infinity();
  OrphanablePtr<SubchannelStreamClient> stream_client_ ABSL_GUARDED_BY(mu_);
};

// This watcher is returned to the LB policy and added to the
// client channel SubchannelWrapper.
class OrcaWatcher : public SubchannelInterface::DataWatcherInterface {
 public:
  OrcaWatcher(Duration report_interval,
              std::unique_ptr<OobBackendMetricWatcher> watcher)
      : report_interval_(report_interval), watcher_(std::move(watcher)) {}

  void Orphan() override {
    if (producer_ != nullptr) {
      producer_->RemoveWatcher(this);
      producer_.reset();
    }
  }

  Duration report_interval() const { return report_interval_; }

  // When the client channel sees this wrapper, it will pass it the real
  // subchannel and the WorkSerializer to use.
  void SetSubchannel(
      Subchannel* subchannel,
      std::shared_ptr<WorkSerializer> work_serializer) override;

  OobBackendMetricWatcher* watcher() const { return watcher_.get(); }

 private:
  Duration report_interval_;
  std::unique_ptr<OobBackendMetricWatcher> watcher_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  RefCountedPtr<OrcaProducer> producer_;
};

//
// OrcaProducer::OrcaStreamEventHandler
//

grpc_slice OrcaProducer::OrcaStreamEventHandler::EncodeSendMessageLocked() {
  upb::Arena arena;
  xds_service_orca_v3_OrcaLoadReportRequest* request =
      xds_service_orca_v3_OrcaLoadReportRequest_new(arena.ptr());
  gpr_timespec timespec = producer_->report_interval_.as_timespec();
  auto* report_interval =
      xds_service_orca_v3_OrcaLoadReportRequest_mutable_report_interval(
          request, arena.ptr());
  google_protobuf_Duration_set_seconds(report_interval, timespec.tv_sec);
  google_protobuf_Duration_set_nanos(report_interval, timespec.tv_nsec);
  size_t buf_length;
  char* buf = xds_service_orca_v3_OrcaLoadReportRequest_serialize(
      request, arena.ptr(), &buf_length);
  grpc_slice request_slice = GRPC_SLICE_MALLOC(buf_length);
  memcpy(GRPC_SLICE_START_PTR(request_slice), buf, buf_length);
  return request_slice;
}

void OrcaProducer::OrcaStreamEventHandler::RecvMessageReadyLocked(
    SubchannelStreamClient* client, char* message, size_t size) {
  LoadBalancingPolicy::BackendMetricAccessor::BackendMetricData
      backend_metric_data;
  if (ParseBackendMetricData(absl::string_view(message, size),
                             &backend_metric_data)) {
// FIXME
    producer_->NotifyWatchers();
  }
}

void OrcaProducer::OrcaStreamEventHandler::RecvTrailingMetadataReadyLocked(
    SubchannelStreamClient* client, grpc_status_code status) override {
  if (status == GRPC_STATUS_UNIMPLEMENTED) {
    static const char kErrorMessage[] =
        "health checking Watch method returned UNIMPLEMENTED; "
        "disabling health checks but assuming server is healthy";
    gpr_log(GPR_ERROR, kErrorMessage);
    if (channelz_node_ != nullptr) {
      channelz_node_->AddTraceEvent(
          channelz::ChannelTrace::Error,
          grpc_slice_from_static_string(kErrorMessage));
    }
    SetHealthStatusLocked(client, GRPC_CHANNEL_READY, kErrorMessage);
  }
}

absl::StatusOr<bool> OrcaProducer::OrcaStreamEventHandler::DecodeResponse(
    char* message, size_t size) {
  // If message is empty, assume unhealthy.
  if (size == 0) {
    return absl::InvalidArgumentError("health check response was empty");
  }
  // Deserialize message.
  upb::Arena arena;
  auto* response_struct = grpc_health_v1_HealthCheckResponse_parse(
      reinterpret_cast<char*>(message), size, arena.ptr());
  if (response_struct == nullptr) {
    // Can't parse message; assume unhealthy.
    return absl::InvalidArgumentError("cannot parse health check response");
  }
  int32_t status = grpc_health_v1_HealthCheckResponse_status(response_struct);
  return status == grpc_health_v1_HealthCheckResponse_SERVING;
}

void OrcaProducer::OrcaStreamEventHandler::SetHealthStatusLocked(
    SubchannelStreamClient* client, grpc_connectivity_state state,
    const char* reason) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthCheckClient %p: setting state=%s reason=%s",
            client, ConnectivityStateName(state), reason);
  }
  watcher_->Notify(state, state == GRPC_CHANNEL_TRANSIENT_FAILURE
                              ? absl::UnavailableError(reason)
                              : absl::Status());
}

//
// OrcaProducer
//

void OrcaProducer::AddWatcher(WeakRefCountedPtr<OrcaWatcher> watcher) {
  MutexLock lock(&mu_);
  Duration watcher_interval = watcher->report_interval();
  watcher_map_[watcher.get()] = std::move(watcher);
  if (watcher_interval < report_interval_) {
    report_interval_ = watcher_interval;
    stream_client_.reset();
    StartStreamLocked();
  }
// FIXME: need to recreate stream client every time subchannel becomes
// connected?
}

void OrcaProducer::RemoveWatcher(OrcaWatcher* watcher) {
  MutexLock lock(&mu_);
  watcher_map_.erase(watcher);
  if (watcher_map_.empty()) {
    stream_client_.reset();
    return;
  }
  Duration new_interval = GetMinIntervalLocked();
  if (new_interval < report_interval_) {
    stream_client_.reset();
    StartStreamLocked();
  }
}

Duration OrcaProducer::GetMinIntervalLocked() const {
  Duration duration = Duration::Infinity();
  for (const auto& p : watcher_map_) {
    Duration watcher_interval = p.first->report_interval();
    if (watcher_interval < duration) duration = watcher_interval;
  }
  return duration;
}

void OrcaProducer::StartStreamLocked() {
  stream_client_ = MakeOrphanable<SubchannelStreamClient>(
      subchannel_->connected_subchannel(), subchannel_->pollset_set(),
      absl::make_unique<OrcaStreamEventHandler>(WeakRef()),
      GRPC_TRACE_FLAG_ENABLED(grpc_orca_client_trace)
          ? "OrcaClient"
          : nullptr);
}

//
// OrcaWatcher
//

void OrcaWatcher::SetSubchannel(
    Subchannel* subchannel, std::shared_ptr<WorkSerializer> work_serializer) {
  work_serializer_ = std::move(work_serializer);
  // Check if our producer is already registered with the subchannel.
  // If not, create a new one, which will register itself with the subchannel.
  auto* p =
      static_cast<OrcaProducer*>(subchannel->GetDataProducer(kProducerType));
  if (p != nullptr) producer_ = p->RefIfNonZero();
  if (producer_ == nullptr) {
    producer_ = MakeRefCounted<OrcaProducer>(subchannel->Ref());
  }
  // Register ourself with the producer.
  producer_->AddWatcher(WeakRef());
}

}  // namespace

RefCountedPtr<SubchannelInterface::DataWatcherInterface>
MakeOobBackendMetricWatcher(Duration report_interval,
                            std::unique_ptr<OobBackendMetricWatcher> watcher) {
  return MakeRefCounted<OrcaWatcher>(report_interval, std::move(watcher));
}

}  // namespace grpc_core
