//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/xds_client/lrs_client.h"

#include <grpc/event_engine/event_engine.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/endpoint/v3/load_report.upb.h"
#include "envoy/service/load_stats/v3/lrs.upb.h"
#include "envoy/service/load_stats/v3/lrs.upbdefs.h"
#include "google/protobuf/duration.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/uri.h"
#include "src/core/xds/xds_client/xds_api.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_locality.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/reflection/def.h"
#include "upb/text/encode.h"

#define GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_XDS_RECONNECT_JITTER 0.2
#define GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS 1000

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

// TODO(roth): Remove this once the feature passes interop tests.
bool XdsOrcaLrsPropagationChangesEnabled() {
  auto value = GetEnv("GRPC_EXPERIMENTAL_XDS_ORCA_LRS_PROPAGATION");
  if (!value.has_value()) return false;
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value->c_str(), &parsed_value);
  return parse_succeeded && parsed_value;
}

namespace {

uint64_t GetAndResetCounter(std::atomic<uint64_t>* from) {
  return from->exchange(0, std::memory_order_relaxed);
}

}  // namespace

//
// LrsClient::ClusterDropStats
//

LrsClient::ClusterDropStats::ClusterDropStats(
    RefCountedPtr<LrsClient> lrs_client, absl::string_view lrs_server,
    absl::string_view cluster_name, absl::string_view eds_service_name)
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(xds_client_refcount)
                     ? "ClusterDropStats"
                     : nullptr),
      lrs_client_(std::move(lrs_client)),
      lrs_server_(lrs_server),
      cluster_name_(cluster_name),
      eds_service_name_(eds_service_name) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client_.get() << "] created drop stats " << this
      << " for {" << lrs_server_ << ", " << cluster_name_ << ", "
      << eds_service_name_ << "}";
}

LrsClient::ClusterDropStats::~ClusterDropStats() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client_.get() << "] destroying drop stats "
      << this << " for {" << lrs_server_ << ", " << cluster_name_ << ", "
      << eds_service_name_ << "}";
  lrs_client_->RemoveClusterDropStats(lrs_server_, cluster_name_,
                                      eds_service_name_, this);
  lrs_client_.reset(DEBUG_LOCATION, "ClusterDropStats");
}

LrsClient::ClusterDropStats::Snapshot
LrsClient::ClusterDropStats::GetSnapshotAndReset() {
  Snapshot snapshot;
  snapshot.uncategorized_drops = GetAndResetCounter(&uncategorized_drops_);
  MutexLock lock(&mu_);
  snapshot.categorized_drops = std::move(categorized_drops_);
  return snapshot;
}

void LrsClient::ClusterDropStats::AddUncategorizedDrops() {
  uncategorized_drops_.fetch_add(1);
}

void LrsClient::ClusterDropStats::AddCallDropped(const std::string& category) {
  MutexLock lock(&mu_);
  ++categorized_drops_[category];
}

//
// LrsClient::ClusterLocalityStats
//

LrsClient::ClusterLocalityStats::ClusterLocalityStats(
    RefCountedPtr<LrsClient> lrs_client, absl::string_view lrs_server,
    absl::string_view cluster_name, absl::string_view eds_service_name,
    RefCountedPtr<XdsLocalityName> name,
    RefCountedPtr<const BackendMetricPropagation> backend_metric_propagation)
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(xds_client_refcount)
                     ? "ClusterLocalityStats"
                     : nullptr),
      lrs_client_(std::move(lrs_client)),
      lrs_server_(lrs_server),
      cluster_name_(cluster_name),
      eds_service_name_(eds_service_name),
      name_(std::move(name)),
      backend_metric_propagation_(std::move(backend_metric_propagation)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client_.get() << "] created locality stats "
      << this << " for {" << lrs_server_ << ", " << cluster_name_ << ", "
      << eds_service_name_ << ", "
      << (name_ == nullptr ? "<none>" : name_->human_readable_string().c_str())
      << ", propagation=" << backend_metric_propagation_->AsString() << "}";
}

LrsClient::ClusterLocalityStats::~ClusterLocalityStats() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client_.get() << "] destroying locality stats "
      << this << " for {" << lrs_server_ << ", " << cluster_name_ << ", "
      << eds_service_name_ << ", "
      << (name_ == nullptr ? "<none>" : name_->human_readable_string().c_str())
      << ", propagation=" << backend_metric_propagation_->AsString() << "}";
  lrs_client_->RemoveClusterLocalityStats(lrs_server_, cluster_name_,
                                          eds_service_name_, name_,
                                          backend_metric_propagation_, this);
  lrs_client_.reset(DEBUG_LOCATION, "ClusterLocalityStats");
}

LrsClient::ClusterLocalityStats::Snapshot
LrsClient::ClusterLocalityStats::GetSnapshotAndReset() {
  Snapshot snapshot;
  for (auto& percpu_stats : stats_) {
    Snapshot percpu_snapshot = {
        GetAndResetCounter(&percpu_stats.total_successful_requests),
        // Don't reset total_requests_in_progress because it's
        // not related to a single reporting interval.
        percpu_stats.total_requests_in_progress.load(std::memory_order_relaxed),
        GetAndResetCounter(&percpu_stats.total_error_requests),
        GetAndResetCounter(&percpu_stats.total_issued_requests),
        {},
        {},
        {},
        {}};
    {
      MutexLock lock(&percpu_stats.backend_metrics_mu);
      percpu_snapshot.cpu_utilization = std::move(percpu_stats.cpu_utilization);
      percpu_snapshot.mem_utilization = std::move(percpu_stats.mem_utilization);
      percpu_snapshot.application_utilization =
          std::move(percpu_stats.application_utilization);
      percpu_snapshot.backend_metrics = std::move(percpu_stats.backend_metrics);
    }
    snapshot += percpu_snapshot;
  }
  return snapshot;
}

void LrsClient::ClusterLocalityStats::AddCallStarted() {
  Stats& stats = stats_.this_cpu();
  stats.total_issued_requests.fetch_add(1, std::memory_order_relaxed);
  stats.total_requests_in_progress.fetch_add(1, std::memory_order_relaxed);
}

void LrsClient::ClusterLocalityStats::AddCallFinished(
    const BackendMetricData* backend_metrics, bool fail) {
  Stats& stats = stats_.this_cpu();
  std::atomic<uint64_t>& to_increment =
      fail ? stats.total_error_requests : stats.total_successful_requests;
  to_increment.fetch_add(1, std::memory_order_relaxed);
  stats.total_requests_in_progress.fetch_add(-1, std::memory_order_acq_rel);
  if (backend_metrics == nullptr) return;
  MutexLock lock(&stats.backend_metrics_mu);
  if (!XdsOrcaLrsPropagationChangesEnabled()) {
    for (const auto& m : backend_metrics->named_metrics) {
      stats.backend_metrics[std::string(m.first)] += BackendMetric(1, m.second);
    }
    return;
  }
  if (backend_metric_propagation_->propagation_bits &
      BackendMetricPropagation::kCpuUtilization) {
    stats.cpu_utilization += BackendMetric(1, backend_metrics->cpu_utilization);
  }
  if (backend_metric_propagation_->propagation_bits &
      BackendMetricPropagation::kMemUtilization) {
    stats.mem_utilization += BackendMetric(1, backend_metrics->mem_utilization);
  }
  if (backend_metric_propagation_->propagation_bits &
      BackendMetricPropagation::kApplicationUtilization) {
    stats.application_utilization +=
        BackendMetric(1, backend_metrics->application_utilization);
  }
  if (backend_metric_propagation_->propagation_bits &
          BackendMetricPropagation::kNamedMetricsAll ||
      !backend_metric_propagation_->named_metric_keys.empty()) {
    for (const auto& m : backend_metrics->named_metrics) {
      if (backend_metric_propagation_->propagation_bits &
              BackendMetricPropagation::kNamedMetricsAll ||
          backend_metric_propagation_->named_metric_keys.contains(m.first)) {
        stats.backend_metrics[absl::StrCat("named_metrics.", m.first)] +=
            BackendMetric(1, m.second);
      }
    }
  }
}

//
// Internal class declarations
//

// A call wrapper that can restart a call upon failure.
// The template parameter is the kind of wrapped call.
// TODO(roth): This is basically the same code as in XdsClient, and
// probably very similar to many other places in the codebase.
// Consider refactoring this into a common utility library somehow.
template <typename T>
class LrsClient::LrsChannel::RetryableCall final
    : public InternallyRefCounted<RetryableCall<T>> {
 public:
  explicit RetryableCall(WeakRefCountedPtr<LrsChannel> lrs_channel);

  // Disable thread-safety analysis because this method is called via
  // OrphanablePtr<>, but there's no way to pass the lock annotation
  // through there.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

  void OnCallFinishedLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&LrsClient::mu_);

  T* call() const { return call_.get(); }
  LrsChannel* lrs_channel() const { return lrs_channel_.get(); }

  bool IsCurrentCallOnChannel() const;

 private:
  void StartNewCallLocked();
  void StartRetryTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&LrsClient::mu_);

  void OnRetryTimer();

  // The wrapped xds call that talks to the xds server. It's instantiated
  // every time we start a new call. It's null during call retry backoff.
  OrphanablePtr<T> call_;
  // The owning xds channel.
  WeakRefCountedPtr<LrsChannel> lrs_channel_;

  // Retry state.
  BackOff backoff_;
  absl::optional<EventEngine::TaskHandle> timer_handle_
      ABSL_GUARDED_BY(&LrsClient::mu_);

  bool shutting_down_ = false;
};

// An LRS call to the LRS server.
class LrsClient::LrsChannel::LrsCall final
    : public InternallyRefCounted<LrsCall> {
 public:
  // The ctor and dtor should not be used directly.
  explicit LrsCall(RefCountedPtr<RetryableCall<LrsCall>> retryable_call);

  void Orphan() override;

  RetryableCall<LrsCall>* retryable_call() { return retryable_call_.get(); }
  LrsChannel* lrs_channel() const { return retryable_call_->lrs_channel(); }
  LrsClient* lrs_client() const { return lrs_channel()->lrs_client(); }
  bool seen_response() const { return seen_response_; }

 private:
  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(RefCountedPtr<LrsCall> lrs_call)
        : lrs_call_(std::move(lrs_call)) {}

    void OnRequestSent(bool /*ok*/) override { lrs_call_->OnRequestSent(); }
    void OnRecvMessage(absl::string_view payload) override {
      lrs_call_->OnRecvMessage(payload);
    }
    void OnStatusReceived(absl::Status status) override {
      lrs_call_->OnStatusReceived(std::move(status));
    }

   private:
    RefCountedPtr<LrsCall> lrs_call_;
  };

  // A repeating timer for a particular duration.
  class Timer final : public InternallyRefCounted<Timer> {
   public:
    explicit Timer(RefCountedPtr<LrsCall> lrs_call)
        : lrs_call_(std::move(lrs_call)) {}
    ~Timer() override { lrs_call_.reset(DEBUG_LOCATION, "LRS timer"); }

    // Disable thread-safety analysis because this method is called via
    // OrphanablePtr<>, but there's no way to pass the lock annotation
    // through there.
    void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

    void ScheduleNextReportLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&LrsClient::mu_);

   private:
    bool IsCurrentTimerOnCall() const {
      return this == lrs_call_->timer_.get();
    }
    LrsClient* lrs_client() const { return lrs_call_->lrs_client(); }

    void OnNextReportTimer();

    // The owning LRS call.
    RefCountedPtr<LrsCall> lrs_call_;

    absl::optional<EventEngine::TaskHandle> timer_handle_
        ABSL_GUARDED_BY(&LrsClient::mu_);
  };

  void MaybeScheduleNextReportLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&LrsClient::mu_);

  void SendReportLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&LrsClient::mu_);

  void SendMessageLocked(std::string payload)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&LrsClient::mu_);

  void OnRequestSent();
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  bool IsCurrentCallOnChannel() const;

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<LrsCall>> retryable_call_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
      streaming_call_;

  bool seen_response_ = false;
  bool send_message_pending_ ABSL_GUARDED_BY(&LrsClient::mu_) = false;

  // Load reporting state.
  bool send_all_clusters_ = false;
  std::set<std::string> cluster_names_;  // Asked for by the LRS server.
  Duration load_reporting_interval_;
  bool last_report_counters_were_zero_ = false;
  OrphanablePtr<Timer> timer_;
};

//
// LrsClient::LrsChannel
//

LrsClient::LrsChannel::LrsChannel(
    WeakRefCountedPtr<LrsClient> lrs_client,
    std::shared_ptr<const XdsBootstrap::XdsServer> server)
    : DualRefCounted<LrsChannel>(GRPC_TRACE_FLAG_ENABLED(xds_client_refcount)
                                     ? "LrsChannel"
                                     : nullptr),
      lrs_client_(std::move(lrs_client)),
      server_(std::move(server)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client_.get() << "] creating channel " << this
      << " for server " << server_->server_uri();
  absl::Status status;
  transport_ = lrs_client_->transport_factory_->GetTransport(*server_, &status);
  CHECK(transport_ != nullptr);
  if (!status.ok()) {
    LOG(ERROR) << "Error creating LRS channel to " << server_->server_uri()
               << ": " << status;
  }
}

LrsClient::LrsChannel::~LrsChannel() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client() << "] destroying lrs channel " << this
      << " for server " << server_->server_uri();
  lrs_client_.reset(DEBUG_LOCATION, "LrsChannel");
}

// This method should only ever be called when holding the lock, but we can't
// use a ABSL_EXCLUSIVE_LOCKS_REQUIRED annotation, because Orphan() will be
// called from DualRefCounted::Unref(), which cannot have a lock annotation for
// a lock in this subclass.
void LrsClient::LrsChannel::Orphaned() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client() << "] orphaning lrs channel " << this
      << " for server " << server_->server_uri();
  transport_.reset();
  // At this time, all strong refs are removed, remove from channel map to
  // prevent subsequent subscription from trying to use this LrsChannel as
  // it is shutting down.
  lrs_client_->lrs_channel_map_.erase(server_->Key());
  lrs_call_.reset();
}

void LrsClient::LrsChannel::ResetBackoff() { transport_->ResetBackoff(); }

void LrsClient::LrsChannel::MaybeStartLrsCall() {
  if (lrs_call_ != nullptr) return;
  lrs_call_ = MakeOrphanable<RetryableCall<LrsCall>>(
      WeakRef(DEBUG_LOCATION, "LrsCall"));
}

void LrsClient::LrsChannel::StopLrsCallLocked() {
  lrs_client_->load_report_map_.erase(server_->Key());
  lrs_call_.reset();
}

//
// LrsClient::LrsChannel::RetryableCall<>
//

template <typename T>
LrsClient::LrsChannel::RetryableCall<T>::RetryableCall(
    WeakRefCountedPtr<LrsChannel> lrs_channel)
    : lrs_channel_(std::move(lrs_channel)),
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
void LrsClient::LrsChannel::RetryableCall<T>::Orphan() {
  shutting_down_ = true;
  call_.reset();
  if (timer_handle_.has_value()) {
    lrs_channel()->lrs_client()->engine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  this->Unref(DEBUG_LOCATION, "RetryableCall+orphaned");
}

template <typename T>
void LrsClient::LrsChannel::RetryableCall<T>::OnCallFinishedLocked() {
  // If we saw a response on the current stream, reset backoff.
  if (call_->seen_response()) backoff_.Reset();
  call_.reset();
  // Start retry timer.
  StartRetryTimerLocked();
}

template <typename T>
void LrsClient::LrsChannel::RetryableCall<T>::StartNewCallLocked() {
  if (shutting_down_) return;
  CHECK(lrs_channel_->transport_ != nullptr);
  CHECK(call_ == nullptr);
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_channel()->lrs_client() << "] lrs server "
      << lrs_channel()->server_->server_uri()
      << ": start new call from retryable call " << this;
  call_ = MakeOrphanable<T>(
      this->Ref(DEBUG_LOCATION, "RetryableCall+start_new_call"));
}

template <typename T>
void LrsClient::LrsChannel::RetryableCall<T>::StartRetryTimerLocked() {
  if (shutting_down_) return;
  const Duration delay = backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_channel()->lrs_client() << "] lrs server "
      << lrs_channel()->server_->server_uri()
      << ": call attempt failed; retry timer will fire in " << delay.millis()
      << "ms.";
  timer_handle_ = lrs_channel()->lrs_client()->engine()->RunAfter(
      delay,
      [self = this->Ref(DEBUG_LOCATION, "RetryableCall+retry_timer_start")]() {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        self->OnRetryTimer();
      });
}

template <typename T>
void LrsClient::LrsChannel::RetryableCall<T>::OnRetryTimer() {
  MutexLock lock(&lrs_channel_->lrs_client()->mu_);
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    if (shutting_down_) return;
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[lrs_client " << lrs_channel()->lrs_client() << "] lrs server "
        << lrs_channel()->server_->server_uri()
        << ": retry timer fired (retryable call: " << this << ")";
    StartNewCallLocked();
  }
}

//
// LrsClient::LrsChannel::LrsCall::Timer
//

void LrsClient::LrsChannel::LrsCall::Timer::Orphan() {
  if (timer_handle_.has_value()) {
    lrs_client()->engine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void LrsClient::LrsChannel::LrsCall::Timer::ScheduleNextReportLocked() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client() << "] lrs server "
      << lrs_call_->lrs_channel()->server_->server_uri()
      << ": scheduling next load report in "
      << lrs_call_->load_reporting_interval_;
  timer_handle_ = lrs_client()->engine()->RunAfter(
      lrs_call_->load_reporting_interval_,
      [self = Ref(DEBUG_LOCATION, "timer")]() {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        self->OnNextReportTimer();
      });
}

void LrsClient::LrsChannel::LrsCall::Timer::OnNextReportTimer() {
  MutexLock lock(&lrs_client()->mu_);
  timer_handle_.reset();
  if (IsCurrentTimerOnCall()) lrs_call_->SendReportLocked();
}

//
// LrsClient::LrsChannel::LrsCall
//

LrsClient::LrsChannel::LrsCall::LrsCall(
    RefCountedPtr<RetryableCall<LrsCall>> retryable_call)
    : InternallyRefCounted<LrsCall>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "LrsCall" : nullptr),
      retryable_call_(std::move(retryable_call)) {
  // Init the LRS call. Note that the call will progress every time there's
  // activity in lrs_client()->interested_parties_, which is comprised of
  // the polling entities from client_channel.
  CHECK_NE(lrs_client(), nullptr);
  const char* method =
      "/envoy.service.load_stats.v3.LoadReportingService/StreamLoadStats";
  streaming_call_ = lrs_channel()->transport_->CreateStreamingCall(
      method, std::make_unique<StreamEventHandler>(
                  // Passing the initial ref here.  This ref will go away when
                  // the StreamEventHandler is destroyed.
                  RefCountedPtr<LrsCall>(this)));
  CHECK(streaming_call_ != nullptr);
  // Start the call.
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client() << "] lrs server "
      << lrs_channel()->server_->server_uri()
      << ": starting LRS call (lrs_call=" << this
      << ", streaming_call=" << streaming_call_.get() << ")";
  // Send the initial request.
  std::string serialized_payload = lrs_client()->CreateLrsInitialRequest();
  SendMessageLocked(std::move(serialized_payload));
  // Read initial response.
  streaming_call_->StartRecvMessage();
}

void LrsClient::LrsChannel::LrsCall::Orphan() {
  timer_.reset();
  // Note that the initial ref is held by the StreamEventHandler, which
  // will be destroyed when streaming_call_ is destroyed, which may not happen
  // here, since there may be other refs held to streaming_call_ by internal
  // callbacks.
  streaming_call_.reset();
}

void LrsClient::LrsChannel::LrsCall::MaybeScheduleNextReportLocked() {
  // If there are no more registered stats to report, cancel the call.
  auto it = lrs_client()->load_report_map_.find(lrs_channel()->server_->Key());
  if (it == lrs_client()->load_report_map_.end() ||
      it->second.load_report_map.empty()) {
    it->second.lrs_channel->StopLrsCallLocked();
    return;
  }
  // Don't start if the previous send_message op hasn't completed yet.
  // If this happens, we'll be called again from OnRequestSent().
  if (send_message_pending_) return;
  // Don't start if no LRS response has arrived.
  if (!seen_response()) return;
  // If there is no timer, create one.
  // This happens on the initial response and whenever the interval changes.
  if (timer_ == nullptr) {
    timer_ = MakeOrphanable<Timer>(Ref(DEBUG_LOCATION, "LRS timer"));
  }
  // Schedule the next load report.
  timer_->ScheduleNextReportLocked();
}

bool LrsClient::LoadReportCountersAreZero(
    const ClusterLoadReportMap& snapshot) {
  for (const auto& p : snapshot) {
    const ClusterLoadReport& cluster_snapshot = p.second;
    if (!cluster_snapshot.dropped_requests.IsZero()) return false;
    for (const auto& q : cluster_snapshot.locality_stats) {
      const ClusterLocalityStats::Snapshot& locality_snapshot = q.second;
      if (!locality_snapshot.IsZero()) return false;
    }
  }
  return true;
}

void LrsClient::LrsChannel::LrsCall::SendReportLocked() {
  // Construct snapshot from all reported stats.
  ClusterLoadReportMap snapshot = lrs_client()->BuildLoadReportSnapshotLocked(
      *lrs_channel()->server_, send_all_clusters_, cluster_names_);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  const bool old_val = last_report_counters_were_zero_;
  last_report_counters_were_zero_ = LoadReportCountersAreZero(snapshot);
  if (old_val && last_report_counters_were_zero_) {
    MaybeScheduleNextReportLocked();
    return;
  }
  // Send a request that contains the snapshot.
  std::string serialized_payload =
      lrs_client()->CreateLrsRequest(std::move(snapshot));
  SendMessageLocked(std::move(serialized_payload));
}

void LrsClient::LrsChannel::LrsCall::SendMessageLocked(std::string payload) {
  send_message_pending_ = true;
  streaming_call_->SendMessage(std::move(payload));
}

void LrsClient::LrsChannel::LrsCall::OnRequestSent() {
  MutexLock lock(&lrs_client()->mu_);
  send_message_pending_ = false;
  if (IsCurrentCallOnChannel()) MaybeScheduleNextReportLocked();
}

void LrsClient::LrsChannel::LrsCall::OnRecvMessage(absl::string_view payload) {
  MutexLock lock(&lrs_client()->mu_);
  // If we're no longer the current call, ignore the result.
  if (!IsCurrentCallOnChannel()) return;
  // Start recv after any code branch
  auto cleanup = absl::MakeCleanup(
      [call = streaming_call_.get()]() { call->StartRecvMessage(); });
  // Parse the response.
  bool send_all_clusters = false;
  std::set<std::string> new_cluster_names;
  Duration new_load_reporting_interval;
  absl::Status status = lrs_client()->ParseLrsResponse(
      payload, &send_all_clusters, &new_cluster_names,
      &new_load_reporting_interval);
  if (!status.ok()) {
    LOG(ERROR) << "[lrs_client " << lrs_client() << "] lrs server "
               << lrs_channel()->server_->server_uri()
               << ": LRS response parsing failed: " << status;
    return;
  }
  seen_response_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(xds_client)) {
    LOG(INFO) << "[lrs_client " << lrs_client() << "] lrs server "
              << lrs_channel()->server_->server_uri()
              << ": LRS response received, " << new_cluster_names.size()
              << " cluster names, send_all_clusters=" << send_all_clusters
              << ", load_report_interval="
              << new_load_reporting_interval.millis() << "ms";
    size_t i = 0;
    for (const auto& name : new_cluster_names) {
      LOG(INFO) << "[lrs_client " << lrs_client() << "] cluster_name " << i++
                << ": " << name;
    }
  }
  if (new_load_reporting_interval <
      Duration::Milliseconds(GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS)) {
    new_load_reporting_interval =
        Duration::Milliseconds(GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS);
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[lrs_client " << lrs_client() << "] lrs server "
        << lrs_channel()->server_->server_uri()
        << ": increased load_report_interval to minimum value "
        << GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS << "ms";
  }
  // Ignore identical update.
  if (send_all_clusters == send_all_clusters_ &&
      cluster_names_ == new_cluster_names &&
      load_reporting_interval_ == new_load_reporting_interval) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[lrs_client " << lrs_client() << "] lrs server "
        << lrs_channel()->server_->server_uri()
        << ": incoming LRS response identical to current, ignoring.";
    return;
  }
  // If the interval has changed, we'll need to restart the timer below.
  const bool restart_timer =
      load_reporting_interval_ != new_load_reporting_interval;
  // Record the new config.
  send_all_clusters_ = send_all_clusters;
  cluster_names_ = std::move(new_cluster_names);
  load_reporting_interval_ = new_load_reporting_interval;
  // Restart timer if needed.
  if (restart_timer) {
    timer_.reset();
    MaybeScheduleNextReportLocked();
  }
}

void LrsClient::LrsChannel::LrsCall::OnStatusReceived(absl::Status status) {
  MutexLock lock(&lrs_client()->mu_);
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << lrs_client() << "] lrs server "
      << lrs_channel()->server_->server_uri()
      << ": LRS call status received (lrs_channel=" << lrs_channel()
      << ", lrs_call=" << this << ", streaming_call=" << streaming_call_.get()
      << "): " << status;
  // Ignore status from a stale call.
  if (IsCurrentCallOnChannel()) {
    // Try to restart the call.
    retryable_call_->OnCallFinishedLocked();
  }
}

bool LrsClient::LrsChannel::LrsCall::IsCurrentCallOnChannel() const {
  // If the retryable LRS call is null (which only happens when the lrs
  // channel is shutting down), all the LRS calls are stale.
  if (lrs_channel()->lrs_call_ == nullptr) return false;
  return this == lrs_channel()->lrs_call_->call();
}

//
// LrsClient
//

LrsClient::LrsClient(
    std::shared_ptr<XdsBootstrap> bootstrap, std::string user_agent_name,
    std::string user_agent_version,
    RefCountedPtr<XdsTransportFactory> transport_factory,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine)
    : DualRefCounted<LrsClient>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "LrsClient" : nullptr),
      bootstrap_(std::move(bootstrap)),
      user_agent_name_(std::move(user_agent_name)),
      user_agent_version_(std::move(user_agent_version)),
      transport_factory_(std::move(transport_factory)),
      engine_(std::move(engine)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << this << "] creating lrs client";
}

LrsClient::~LrsClient() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << this << "] destroying lrs client";
}

void LrsClient::Orphaned() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << this << "] shutting down lrs client";
  MutexLock lock(&mu_);
  // We may still be sending lingering queued load report data, so don't
  // just clear the load reporting map, but we do want to clear the refs
  // we're holding to the LrsChannel objects, to make sure that
  // everything shuts down properly.
  for (auto& p : load_report_map_) {
    p.second.lrs_channel.reset(DEBUG_LOCATION, "LrsClient::Orphan()");
  }
}

RefCountedPtr<LrsClient::LrsChannel> LrsClient::GetOrCreateLrsChannelLocked(
    std::shared_ptr<const XdsBootstrap::XdsServer> server, const char* reason) {
  std::string key = server->Key();
  auto it = lrs_channel_map_.find(key);
  if (it != lrs_channel_map_.end()) {
    return it->second->Ref(DEBUG_LOCATION, reason);
  }
  // Channel not found, so create a new one.
  auto lrs_channel = MakeRefCounted<LrsChannel>(
      WeakRef(DEBUG_LOCATION, "LrsChannel"), std::move(server));
  lrs_channel_map_[std::move(key)] = lrs_channel.get();
  return lrs_channel;
}

RefCountedPtr<LrsClient::ClusterDropStats> LrsClient::AddClusterDropStats(
    std::shared_ptr<const XdsBootstrap::XdsServer> lrs_server,
    absl::string_view cluster_name, absl::string_view eds_service_name) {
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  RefCountedPtr<ClusterDropStats> cluster_drop_stats;
  {
    MutexLock lock(&mu_);
    // We jump through some hoops here to make sure that the
    // absl::string_views stored in the ClusterDropStats object point
    // to the strings in the load_report_map_ keys, so that
    // they have the same lifetime.
    auto server_it =
        load_report_map_.emplace(lrs_server->Key(), LoadReportServer()).first;
    if (server_it->second.lrs_channel == nullptr) {
      server_it->second.lrs_channel = GetOrCreateLrsChannelLocked(
          lrs_server, "load report map (drop stats)");
    }
    auto load_report_it = server_it->second.load_report_map
                              .emplace(std::move(key), LoadReportState())
                              .first;
    LoadReportState& load_report_state = load_report_it->second;
    if (load_report_state.drop_stats != nullptr) {
      cluster_drop_stats = load_report_state.drop_stats->RefIfNonZero();
    }
    if (cluster_drop_stats == nullptr) {
      if (load_report_state.drop_stats != nullptr) {
        load_report_state.deleted_drop_stats +=
            load_report_state.drop_stats->GetSnapshotAndReset();
      }
      cluster_drop_stats = MakeRefCounted<ClusterDropStats>(
          Ref(DEBUG_LOCATION, "DropStats"), server_it->first /*lrs_server*/,
          load_report_it->first.first /*cluster_name*/,
          load_report_it->first.second /*eds_service_name*/);
      load_report_state.drop_stats = cluster_drop_stats.get();
    }
    server_it->second.lrs_channel->MaybeStartLrsCall();
  }
  return cluster_drop_stats;
}

void LrsClient::RemoveClusterDropStats(
    absl::string_view lrs_server_key, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    LrsClient::ClusterDropStats* cluster_drop_stats) {
  MutexLock lock(&mu_);
  auto server_it = load_report_map_.find(lrs_server_key);
  if (server_it == load_report_map_.end()) return;
  auto load_report_it = server_it->second.load_report_map.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (load_report_it == server_it->second.load_report_map.end()) return;
  LoadReportState& load_report_state = load_report_it->second;
  if (load_report_state.drop_stats == cluster_drop_stats) {
    // Record final snapshot in deleted_drop_stats, which will be
    // added to the next load report.
    load_report_state.deleted_drop_stats +=
        load_report_state.drop_stats->GetSnapshotAndReset();
    load_report_state.drop_stats = nullptr;
  }
}

RefCountedPtr<LrsClient::ClusterLocalityStats>
LrsClient::AddClusterLocalityStats(
    std::shared_ptr<const XdsBootstrap::XdsServer> lrs_server,
    absl::string_view cluster_name, absl::string_view eds_service_name,
    RefCountedPtr<XdsLocalityName> locality,
    RefCountedPtr<const BackendMetricPropagation> backend_metric_propagation) {
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  RefCountedPtr<ClusterLocalityStats> cluster_locality_stats;
  {
    MutexLock lock(&mu_);
    // We jump through some hoops here to make sure that the
    // absl::string_views stored in the ClusterLocalityStats object point
    // to the strings in the load_report_map_ keys, so that
    // they have the same lifetime.
    auto server_it =
        load_report_map_.emplace(lrs_server->Key(), LoadReportServer()).first;
    if (server_it->second.lrs_channel == nullptr) {
      server_it->second.lrs_channel = GetOrCreateLrsChannelLocked(
          std::move(lrs_server), "load report map (locality stats)");
    }
    auto load_report_it = server_it->second.load_report_map
                              .emplace(std::move(key), LoadReportState())
                              .first;
    LoadReportState& load_report_state = load_report_it->second;
    LoadReportState::LocalityState& locality_state =
        load_report_state.locality_stats[locality];
    ClusterLocalityStats*& locality_stats =
        locality_state.propagation_stats[backend_metric_propagation];
    if (locality_stats != nullptr) {
      cluster_locality_stats = locality_stats->RefIfNonZero();
    }
    if (cluster_locality_stats == nullptr) {
      if (locality_stats != nullptr) {
        locality_state.deleted_locality_stats +=
            locality_stats->GetSnapshotAndReset();
      }
      cluster_locality_stats = MakeRefCounted<ClusterLocalityStats>(
          Ref(DEBUG_LOCATION, "LocalityStats"), server_it->first /*lrs_server*/,
          load_report_it->first.first /*cluster_name*/,
          load_report_it->first.second /*eds_service_name*/,
          std::move(locality), std::move(backend_metric_propagation));
      locality_stats = cluster_locality_stats.get();
    }
    server_it->second.lrs_channel->MaybeStartLrsCall();
  }
  return cluster_locality_stats;
}

void LrsClient::RemoveClusterLocalityStats(
    absl::string_view lrs_server_key, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    const RefCountedPtr<XdsLocalityName>& locality,
    const RefCountedPtr<const BackendMetricPropagation>&
        backend_metric_propagation,
    ClusterLocalityStats* cluster_locality_stats) {
  MutexLock lock(&mu_);
  auto server_it = load_report_map_.find(lrs_server_key);
  if (server_it == load_report_map_.end()) return;
  auto load_report_it = server_it->second.load_report_map.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (load_report_it == server_it->second.load_report_map.end()) return;
  LoadReportState& load_report_state = load_report_it->second;
  auto locality_it = load_report_state.locality_stats.find(locality);
  if (locality_it == load_report_state.locality_stats.end()) return;
  LoadReportState::LocalityState& locality_state = locality_it->second;
  auto propagation_it =
      locality_state.propagation_stats.find(backend_metric_propagation);
  if (propagation_it == locality_state.propagation_stats.end()) return;
  ClusterLocalityStats* locality_stats = propagation_it->second;
  if (locality_stats == cluster_locality_stats) {
    // Record final snapshot in deleted_locality_stats, which will be
    // added to the next load report.
    locality_state.deleted_locality_stats +=
        locality_stats->GetSnapshotAndReset();
    locality_state.propagation_stats.erase(propagation_it);
  }
}

void LrsClient::ResetBackoff() {
  MutexLock lock(&mu_);
  for (auto& p : lrs_channel_map_) {
    p.second->ResetBackoff();
  }
}

LrsClient::ClusterLoadReportMap LrsClient::BuildLoadReportSnapshotLocked(
    const XdsBootstrap::XdsServer& lrs_server, bool send_all_clusters,
    const std::set<std::string>& clusters) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[lrs_client " << this << "] start building load report";
  ClusterLoadReportMap snapshot_map;
  auto server_it = load_report_map_.find(lrs_server.Key());
  if (server_it == load_report_map_.end()) return snapshot_map;
  auto& load_report_map = server_it->second.load_report_map;
  for (auto load_report_it = load_report_map.begin();
       load_report_it != load_report_map.end();) {
    // Cluster key is cluster and EDS service name.
    const auto& cluster_key = load_report_it->first;
    LoadReportState& load_report = load_report_it->second;
    // If the CDS response for a cluster indicates to use LRS but the
    // LRS server does not say that it wants reports for this cluster,
    // then we'll have stats objects here whose data we're not going to
    // include in the load report.  However, we still need to clear out
    // the data from the stats objects, so that if the LRS server starts
    // asking for the data in the future, we don't incorrectly include
    // data from previous reporting intervals in that future report.
    const bool record_stats =
        send_all_clusters || clusters.find(cluster_key.first) != clusters.end();
    ClusterLoadReport snapshot;
    // Aggregate drop stats.
    snapshot.dropped_requests = std::move(load_report.deleted_drop_stats);
    if (load_report.drop_stats != nullptr) {
      snapshot.dropped_requests +=
          load_report.drop_stats->GetSnapshotAndReset();
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[lrs_client " << this << "] cluster=" << cluster_key.first
          << " eds_service_name=" << cluster_key.second
          << " drop_stats=" << load_report.drop_stats;
    }
    // Aggregate locality stats.
    for (auto it = load_report.locality_stats.begin();
         it != load_report.locality_stats.end();) {
      const RefCountedPtr<XdsLocalityName>& locality_name = it->first;
      auto& locality_state = it->second;
      ClusterLocalityStats::Snapshot& locality_snapshot =
          snapshot.locality_stats[locality_name];
      locality_snapshot = std::move(locality_state.deleted_locality_stats);
      for (const auto& p : locality_state.propagation_stats) {
        ClusterLocalityStats* locality_stats = p.second;
        if (locality_stats != nullptr) {
          locality_snapshot += locality_stats->GetSnapshotAndReset();
          GRPC_TRACE_LOG(xds_client, INFO)
              << "[lrs_client " << this
              << "] cluster=" << cluster_key.first.c_str()
              << " eds_service_name=" << cluster_key.second.c_str()
              << " locality=" << locality_name->human_readable_string().c_str()
              << " propagation=" << p.first->AsString()
              << " locality_stats=" << locality_stats;
        }
      }
      // If the only thing left in this entry was final snapshots from
      // deleted locality stats objects, remove the entry.
      if (locality_state.propagation_stats.empty()) {
        it = load_report.locality_stats.erase(it);
      } else {
        ++it;
      }
    }
    // Compute load report interval.
    const Timestamp now = Timestamp::Now();
    snapshot.load_report_interval = now - load_report.last_report_time;
    load_report.last_report_time = now;
    // Record snapshot.
    if (record_stats) {
      snapshot_map[cluster_key] = std::move(snapshot);
    }
    // If the only thing left in this entry was final snapshots from
    // deleted stats objects, remove the entry.
    if (load_report.locality_stats.empty() &&
        load_report.drop_stats == nullptr) {
      load_report_it = load_report_map.erase(load_report_it);
    } else {
      ++load_report_it;
    }
  }
  return snapshot_map;
}

namespace {

struct LrsApiContext {
  LrsClient* client;
  upb_DefPool* def_pool;
  upb_Arena* arena;
};

void MaybeLogLrsRequest(
    const LrsApiContext& context,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_client) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_load_stats_v3_LoadStatsRequest_getmsgdef(
            context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(request), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[lrs_client " << context.client
            << "] constructed LRS request: " << buf;
  }
}

std::string SerializeLrsRequest(
    const LrsApiContext& context,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  size_t output_length;
  char* output = envoy_service_load_stats_v3_LoadStatsRequest_serialize(
      request, context.arena, &output_length);
  return std::string(output, output_length);
}

}  // namespace

std::string LrsClient::CreateLrsInitialRequest() {
  upb::Arena arena;
  const LrsApiContext context = {this, def_pool_.ptr(), arena.ptr()};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_config_core_v3_Node* node_msg =
      envoy_service_load_stats_v3_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateXdsNode(bootstrap_->node(), user_agent_name_, user_agent_version_,
                  node_msg, arena.ptr());
  envoy_config_core_v3_Node_add_client_features(
      node_msg,
      upb_StringView_FromString("envoy.lrs.supports_send_all_clusters"),
      arena.ptr());
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

namespace {

void MaybeAddUnnamedMetric(
    const LrsApiContext& context,
    const LrsClient::ClusterLocalityStats::BackendMetric& backend_metric,
    envoy_config_endpoint_v3_UnnamedEndpointLoadMetricStats* (*add_field)(
        envoy_config_endpoint_v3_UpstreamLocalityStats*, upb_Arena*),
    envoy_config_endpoint_v3_UpstreamLocalityStats* output) {
  if (backend_metric.IsZero()) return;
  auto* metric_proto = add_field(output, context.arena);
  envoy_config_endpoint_v3_UnnamedEndpointLoadMetricStats_set_num_requests_finished_with_metric(
      metric_proto, backend_metric.num_requests_finished_with_metric);
  envoy_config_endpoint_v3_UnnamedEndpointLoadMetricStats_set_total_metric_value(
      metric_proto, backend_metric.total_metric_value);
}

void LocalityStatsPopulate(
    const LrsApiContext& context,
    envoy_config_endpoint_v3_UpstreamLocalityStats* output,
    const XdsLocalityName& locality_name,
    const LrsClient::ClusterLocalityStats::Snapshot& snapshot) {
  // Set locality.
  envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_locality(
          output, context.arena);
  if (!locality_name.region().empty()) {
    envoy_config_core_v3_Locality_set_region(
        locality, StdStringToUpbString(locality_name.region()));
  }
  if (!locality_name.zone().empty()) {
    envoy_config_core_v3_Locality_set_zone(
        locality, StdStringToUpbString(locality_name.zone()));
  }
  if (!locality_name.sub_zone().empty()) {
    envoy_config_core_v3_Locality_set_sub_zone(
        locality, StdStringToUpbString(locality_name.sub_zone()));
  }
  // Set total counts.
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_successful_requests(
      output, snapshot.total_successful_requests);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_requests_in_progress(
      output, snapshot.total_requests_in_progress);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_error_requests(
      output, snapshot.total_error_requests);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_issued_requests(
      output, snapshot.total_issued_requests);
  // Add backend metrics.
  MaybeAddUnnamedMetric(
      context, snapshot.cpu_utilization,
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_cpu_utilization,
      output);
  MaybeAddUnnamedMetric(
      context, snapshot.mem_utilization,
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_mem_utilization,
      output);
  MaybeAddUnnamedMetric(
      context, snapshot.application_utilization,
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_application_utilization,
      output);
  for (const auto& p : snapshot.backend_metrics) {
    const std::string& metric_name = p.first;
    const LrsClient::ClusterLocalityStats::BackendMetric& metric_value =
        p.second;
    envoy_config_endpoint_v3_EndpointLoadMetricStats* load_metric =
        envoy_config_endpoint_v3_UpstreamLocalityStats_add_load_metric_stats(
            output, context.arena);
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_metric_name(
        load_metric, StdStringToUpbString(metric_name));
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_num_requests_finished_with_metric(
        load_metric, metric_value.num_requests_finished_with_metric);
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_total_metric_value(
        load_metric, metric_value.total_metric_value);
  }
}

}  // namespace

std::string LrsClient::CreateLrsRequest(
    ClusterLoadReportMap cluster_load_report_map) {
  upb::Arena arena;
  const LrsApiContext context = {this, def_pool_.ptr(), arena.ptr()};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  for (auto& p : cluster_load_report_map) {
    const std::string& cluster_name = p.first.first;
    const std::string& eds_service_name = p.first.second;
    const ClusterLoadReport& load_report = p.second;
    // Add cluster stats.
    envoy_config_endpoint_v3_ClusterStats* cluster_stats =
        envoy_service_load_stats_v3_LoadStatsRequest_add_cluster_stats(
            request, arena.ptr());
    // Set the cluster name.
    envoy_config_endpoint_v3_ClusterStats_set_cluster_name(
        cluster_stats, StdStringToUpbString(cluster_name));
    // Set EDS service name, if non-empty.
    if (!eds_service_name.empty()) {
      envoy_config_endpoint_v3_ClusterStats_set_cluster_service_name(
          cluster_stats, StdStringToUpbString(eds_service_name));
    }
    // Add locality stats.
    for (const auto& p : load_report.locality_stats) {
      const XdsLocalityName& locality_name = *p.first;
      const auto& snapshot = p.second;
      envoy_config_endpoint_v3_UpstreamLocalityStats* locality_stats =
          envoy_config_endpoint_v3_ClusterStats_add_upstream_locality_stats(
              cluster_stats, arena.ptr());
      LocalityStatsPopulate(context, locality_stats, locality_name, snapshot);
    }
    // Add dropped requests.
    uint64_t total_dropped_requests = 0;
    for (const auto& p : load_report.dropped_requests.categorized_drops) {
      const std::string& category = p.first;
      const uint64_t count = p.second;
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests* dropped_requests =
          envoy_config_endpoint_v3_ClusterStats_add_dropped_requests(
              cluster_stats, arena.ptr());
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests_set_category(
          dropped_requests, StdStringToUpbString(category));
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests_set_dropped_count(
          dropped_requests, count);
      total_dropped_requests += count;
    }
    total_dropped_requests += load_report.dropped_requests.uncategorized_drops;
    // Set total dropped requests.
    envoy_config_endpoint_v3_ClusterStats_set_total_dropped_requests(
        cluster_stats, total_dropped_requests);
    // Set real load report interval.
    gpr_timespec timespec = load_report.load_report_interval.as_timespec();
    google_protobuf_Duration* load_report_interval =
        envoy_config_endpoint_v3_ClusterStats_mutable_load_report_interval(
            cluster_stats, arena.ptr());
    google_protobuf_Duration_set_seconds(load_report_interval, timespec.tv_sec);
    google_protobuf_Duration_set_nanos(load_report_interval, timespec.tv_nsec);
  }
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

namespace {

void MaybeLogLrsResponse(
    const LrsApiContext& context,
    const envoy_service_load_stats_v3_LoadStatsResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_client) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_load_stats_v3_LoadStatsResponse_getmsgdef(
            context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(response), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[lrs_client " << context.client
            << "] received LRS response: " << buf;
  }
}

}  // namespace

absl::Status LrsClient::ParseLrsResponse(absl::string_view encoded_response,
                                         bool* send_all_clusters,
                                         std::set<std::string>* cluster_names,
                                         Duration* load_reporting_interval) {
  upb::Arena arena;
  // Decode the response.
  const envoy_service_load_stats_v3_LoadStatsResponse* decoded_response =
      envoy_service_load_stats_v3_LoadStatsResponse_parse(
          encoded_response.data(), encoded_response.size(), arena.ptr());
  // Parse the response.
  if (decoded_response == nullptr) {
    return absl::UnavailableError("Can't decode response.");
  }
  const LrsApiContext context = {this, def_pool_.ptr(), arena.ptr()};
  MaybeLogLrsResponse(context, decoded_response);
  // Check send_all_clusters.
  if (envoy_service_load_stats_v3_LoadStatsResponse_send_all_clusters(
          decoded_response)) {
    *send_all_clusters = true;
  } else {
    // Store the cluster names.
    size_t size;
    const upb_StringView* clusters =
        envoy_service_load_stats_v3_LoadStatsResponse_clusters(decoded_response,
                                                               &size);
    for (size_t i = 0; i < size; ++i) {
      cluster_names->emplace(UpbStringToStdString(clusters[i]));
    }
  }
  // Get the load report interval.
  const google_protobuf_Duration* load_reporting_interval_duration =
      envoy_service_load_stats_v3_LoadStatsResponse_load_reporting_interval(
          decoded_response);
  *load_reporting_interval = Duration::FromSecondsAndNanoseconds(
      google_protobuf_Duration_seconds(load_reporting_interval_duration),
      google_protobuf_Duration_nanos(load_reporting_interval_duration));
  return absl::OkStatus();
}

}  // namespace grpc_core
