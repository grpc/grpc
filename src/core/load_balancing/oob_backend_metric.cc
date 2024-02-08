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

#include "src/core/load_balancing/oob_backend_metric.h"

#include <string.h>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/duration.upb.h"
#include "upb/mem/arena.hpp"
#include "xds/service/orca/v3/orca.upb.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/client_channel/backend_metric.h"
#include "src/core/client_channel/client_channel_channelz.h"
#include "src/core/load_balancing/oob_backend_metric_internal.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/client_channel/subchannel_stream_client.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

TraceFlag grpc_orca_client_trace(false, "orca_client");

//
// OrcaProducer::ConnectivityWatcher
//

class OrcaProducer::ConnectivityWatcher
    : public Subchannel::ConnectivityStateWatcherInterface {
 public:
  explicit ConnectivityWatcher(WeakRefCountedPtr<OrcaProducer> producer)
      : producer_(std::move(producer)),
        interested_parties_(grpc_pollset_set_create()) {}

  ~ConnectivityWatcher() override {
    grpc_pollset_set_destroy(interested_parties_);
  }

  void OnConnectivityStateChange(
      RefCountedPtr<ConnectivityStateWatcherInterface> self,
      grpc_connectivity_state state, const absl::Status&) override {
    producer_->OnConnectivityStateChange(state);
    self.reset();
  }

  grpc_pollset_set* interested_parties() override {
    return interested_parties_;
  }

 private:
  WeakRefCountedPtr<OrcaProducer> producer_;
  grpc_pollset_set* interested_parties_;
};

//
// OrcaProducer::OrcaStreamEventHandler
//

class OrcaProducer::OrcaStreamEventHandler
    : public SubchannelStreamClient::CallEventHandler {
 public:
  OrcaStreamEventHandler(WeakRefCountedPtr<OrcaProducer> producer,
                         Duration report_interval)
      : producer_(std::move(producer)), report_interval_(report_interval) {}

  Slice GetPathLocked() override {
    return Slice::FromStaticString(
        "/xds.service.orca.v3.OpenRcaService/StreamCoreMetrics");
  }

  void OnCallStartLocked(SubchannelStreamClient* /*client*/) override {}

  void OnRetryTimerStartLocked(SubchannelStreamClient* /*client*/) override {}

  grpc_slice EncodeSendMessageLocked() override {
    upb::Arena arena;
    xds_service_orca_v3_OrcaLoadReportRequest* request =
        xds_service_orca_v3_OrcaLoadReportRequest_new(arena.ptr());
    gpr_timespec timespec = report_interval_.as_timespec();
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

  absl::Status RecvMessageReadyLocked(
      SubchannelStreamClient* /*client*/,
      absl::string_view serialized_message) override {
    auto* allocator = new BackendMetricAllocator(producer_);
    auto* backend_metric_data =
        ParseBackendMetricData(serialized_message, allocator);
    if (backend_metric_data == nullptr) {
      delete allocator;
      return absl::InvalidArgumentError("unable to parse Orca response");
    }
    allocator->AsyncNotifyWatchersAndDelete();
    return absl::OkStatus();
  }

  void RecvTrailingMetadataReadyLocked(SubchannelStreamClient* /*client*/,
                                       grpc_status_code status) override {
    if (status == GRPC_STATUS_UNIMPLEMENTED) {
      static const char kErrorMessage[] =
          "Orca stream returned UNIMPLEMENTED; disabling";
      gpr_log(GPR_ERROR, kErrorMessage);
      auto* channelz_node = producer_->subchannel_->channelz_node();
      if (channelz_node != nullptr) {
        channelz_node->AddTraceEvent(
            channelz::ChannelTrace::Error,
            grpc_slice_from_static_string(kErrorMessage));
      }
    }
  }

 private:
  // This class acts as storage for the parsed backend metric data.  It
  // is injected into ParseBackendMetricData() as an allocator that
  // returns internal storage.  It then also acts as a place to hold
  // onto the data during an async hop into the ExecCtx before sending
  // notifications, which avoids lock inversion problems due to
  // acquiring producer_->mu_ while holding the lock from inside of
  // SubchannelStreamClient.
  class BackendMetricAllocator : public BackendMetricAllocatorInterface {
   public:
    explicit BackendMetricAllocator(WeakRefCountedPtr<OrcaProducer> producer)
        : producer_(std::move(producer)) {}

    BackendMetricData* AllocateBackendMetricData() override {
      return &backend_metric_data_;
    }

    char* AllocateString(size_t size) override {
      char* string = static_cast<char*>(gpr_malloc(size));
      string_storage_.emplace_back(string);
      return string;
    }

    // Notifies watchers asynchronously and then deletes the
    // BackendMetricAllocator object.
    void AsyncNotifyWatchersAndDelete() {
      GRPC_CLOSURE_INIT(&closure_, NotifyWatchersInExecCtx, this, nullptr);
      ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
    }

   private:
    static void NotifyWatchersInExecCtx(void* arg,
                                        grpc_error_handle /*error*/) {
      auto* self = static_cast<BackendMetricAllocator*>(arg);
      self->producer_->NotifyWatchers(self->backend_metric_data_);
      delete self;
    }

    WeakRefCountedPtr<OrcaProducer> producer_;
    BackendMetricData backend_metric_data_;
    std::vector<UniquePtr<char>> string_storage_;
    grpc_closure closure_;
  };

  WeakRefCountedPtr<OrcaProducer> producer_;
  const Duration report_interval_;
};

//
// OrcaProducer
//

void OrcaProducer::Start(RefCountedPtr<Subchannel> subchannel) {
  subchannel_ = std::move(subchannel);
  connected_subchannel_ = subchannel_->connected_subchannel();
  auto connectivity_watcher =
      MakeRefCounted<ConnectivityWatcher>(WeakRefAsSubclass<OrcaProducer>());
  connectivity_watcher_ = connectivity_watcher.get();
  subchannel_->WatchConnectivityState(std::move(connectivity_watcher));
}

void OrcaProducer::Orphan() {
  {
    MutexLock lock(&mu_);
    stream_client_.reset();
  }
  GPR_ASSERT(subchannel_ != nullptr);  // Should not be called before Start().
  subchannel_->CancelConnectivityStateWatch(connectivity_watcher_);
  subchannel_->RemoveDataProducer(this);
}

void OrcaProducer::AddWatcher(OrcaWatcher* watcher) {
  MutexLock lock(&mu_);
  watchers_.insert(watcher);
  Duration watcher_interval = watcher->report_interval();
  if (watcher_interval < report_interval_) {
    report_interval_ = watcher_interval;
    stream_client_.reset();
    MaybeStartStreamLocked();
  }
}

void OrcaProducer::RemoveWatcher(OrcaWatcher* watcher) {
  MutexLock lock(&mu_);
  watchers_.erase(watcher);
  if (watchers_.empty()) {
    stream_client_.reset();
    return;
  }
  Duration new_interval = GetMinIntervalLocked();
  if (new_interval < report_interval_) {
    report_interval_ = new_interval;
    stream_client_.reset();
    MaybeStartStreamLocked();
  }
}

Duration OrcaProducer::GetMinIntervalLocked() const {
  Duration duration = Duration::Infinity();
  for (OrcaWatcher* watcher : watchers_) {
    Duration watcher_interval = watcher->report_interval();
    if (watcher_interval < duration) duration = watcher_interval;
  }
  return duration;
}

void OrcaProducer::MaybeStartStreamLocked() {
  if (connected_subchannel_ == nullptr) return;
  stream_client_ = MakeOrphanable<SubchannelStreamClient>(
      connected_subchannel_, subchannel_->pollset_set(),
      std::make_unique<OrcaStreamEventHandler>(
          WeakRefAsSubclass<OrcaProducer>(), report_interval_),
      GRPC_TRACE_FLAG_ENABLED(grpc_orca_client_trace) ? "OrcaClient" : nullptr);
}

void OrcaProducer::NotifyWatchers(
    const BackendMetricData& backend_metric_data) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_orca_client_trace)) {
    gpr_log(GPR_INFO, "OrcaProducer %p: reporting backend metrics to watchers",
            this);
  }
  MutexLock lock(&mu_);
  for (OrcaWatcher* watcher : watchers_) {
    watcher->watcher()->OnBackendMetricReport(backend_metric_data);
  }
}

void OrcaProducer::OnConnectivityStateChange(grpc_connectivity_state state) {
  MutexLock lock(&mu_);
  if (state == GRPC_CHANNEL_READY) {
    connected_subchannel_ = subchannel_->connected_subchannel();
    if (!watchers_.empty()) MaybeStartStreamLocked();
  } else {
    connected_subchannel_.reset();
    stream_client_.reset();
  }
}

//
// OrcaWatcher
//

OrcaWatcher::~OrcaWatcher() {
  if (producer_ != nullptr) producer_->RemoveWatcher(this);
}

void OrcaWatcher::SetSubchannel(Subchannel* subchannel) {
  bool created = false;
  // Check if our producer is already registered with the subchannel.
  // If not, create a new one.
  subchannel->GetOrAddDataProducer(
      OrcaProducer::Type(), [&](Subchannel::DataProducerInterface** producer) {
        if (*producer != nullptr) {
          producer_ =
              (*producer)->RefIfNonZero().TakeAsSubclass<OrcaProducer>();
        }
        if (producer_ == nullptr) {
          producer_ = MakeRefCounted<OrcaProducer>();
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
  producer_->AddWatcher(this);
}

std::unique_ptr<SubchannelInterface::DataWatcherInterface>
MakeOobBackendMetricWatcher(Duration report_interval,
                            std::unique_ptr<OobBackendMetricWatcher> watcher) {
  return std::make_unique<OrcaWatcher>(report_interval, std::move(watcher));
}

}  // namespace grpc_core
