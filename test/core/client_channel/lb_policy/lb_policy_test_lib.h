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

#ifndef GRPC_TEST_CORE_CLIENT_CHANNEL_LB_POLICY_LB_POLICY_TEST_LIB_H
#define GRPC_TEST_CORE_CLIENT_CHANNEL_LB_POLICY_LB_POLICY_TEST_LIB_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <ratio>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h"
#include "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric_internal.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/event_engine/mock_event_engine.h"

namespace grpc_core {
namespace testing {

class LoadBalancingPolicyTest : public ::testing::Test {
 protected:
  using CallAttributes = std::vector<
      std::unique_ptr<ServiceConfigCallData::CallAttributeInterface>>;

  // Channel-level subchannel state for a specific address and channel args.
  // This is analogous to the real subchannel in the ClientChannel code.
  class SubchannelState {
   public:
    // A fake SubchannelInterface object, to be returned to the LB
    // policy when it calls the helper's CreateSubchannel() method.
    // There may be multiple FakeSubchannel objects associated with a
    // given SubchannelState object.
    class FakeSubchannel : public SubchannelInterface {
     public:
      FakeSubchannel(SubchannelState* state,
                     std::shared_ptr<WorkSerializer> work_serializer)
          : state_(state), work_serializer_(std::move(work_serializer)) {}

      ~FakeSubchannel() override {
        if (orca_watcher_ != nullptr) {
          MutexLock lock(&state_->backend_metric_watcher_mu_);
          state_->watchers_.erase(orca_watcher_.get());
        }
      }

      SubchannelState* state() const { return state_; }

     private:
      // Converts between
      // SubchannelInterface::ConnectivityStateWatcherInterface and
      // ConnectivityStateWatcherInterface.
      class WatcherWrapper : public AsyncConnectivityStateWatcherInterface {
       public:
        WatcherWrapper(
            std::shared_ptr<WorkSerializer> work_serializer,
            std::unique_ptr<
                SubchannelInterface::ConnectivityStateWatcherInterface>
                watcher)
            : AsyncConnectivityStateWatcherInterface(
                  std::move(work_serializer)),
              watcher_(std::move(watcher)) {}

        void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                       const absl::Status& status) override {
          watcher_->OnConnectivityStateChange(new_state, status);
        }

       private:
        std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
            watcher_;
      };

      void WatchConnectivityState(
          std::unique_ptr<
              SubchannelInterface::ConnectivityStateWatcherInterface>
              watcher) override {
        auto watcher_wrapper = MakeOrphanable<WatcherWrapper>(
            work_serializer_, std::move(watcher));
        watcher_map_[watcher.get()] = watcher_wrapper.get();
        MutexLock lock(&state_->mu_);
        state_->state_tracker_.AddWatcher(GRPC_CHANNEL_SHUTDOWN,
                                          std::move(watcher_wrapper));
      }

      void CancelConnectivityStateWatch(
          ConnectivityStateWatcherInterface* watcher) override {
        auto it = watcher_map_.find(watcher);
        if (it == watcher_map_.end()) return;
        MutexLock lock(&state_->mu_);
        state_->state_tracker_.RemoveWatcher(it->second);
        watcher_map_.erase(it);
      }

      void RequestConnection() override {
        MutexLock lock(&state_->requested_connection_mu_);
        state_->requested_connection_ = true;
      }

      void AddDataWatcher(
          std::unique_ptr<DataWatcherInterface> watcher) override {
        MutexLock lock(&state_->backend_metric_watcher_mu_);
        GPR_ASSERT(orca_watcher_ == nullptr);
        orca_watcher_.reset(static_cast<OrcaWatcher*>(watcher.release()));
        state_->watchers_.insert(orca_watcher_.get());
      }

      // Don't need this method, so it's a no-op.
      void ResetBackoff() override {}

      SubchannelState* state_;
      std::shared_ptr<WorkSerializer> work_serializer_;
      std::map<SubchannelInterface::ConnectivityStateWatcherInterface*,
               WatcherWrapper*>
          watcher_map_;
      std::unique_ptr<OrcaWatcher> orca_watcher_;
    };

    explicit SubchannelState(absl::string_view address)
        : address_(address), state_tracker_("LoadBalancingPolicyTest") {}

    const std::string& address() const { return address_; }

    void AssertValidConnectivityStateTransition(
        grpc_connectivity_state from_state, grpc_connectivity_state to_state,
        SourceLocation location = SourceLocation()) {
      switch (from_state) {
        case GRPC_CHANNEL_IDLE:
          ASSERT_EQ(to_state, GRPC_CHANNEL_CONNECTING)
              << ConnectivityStateName(from_state) << "=>"
              << ConnectivityStateName(to_state) << "\n"
              << location.file() << ":" << location.line();
          break;
        case GRPC_CHANNEL_CONNECTING:
          ASSERT_THAT(to_state,
                      ::testing::AnyOf(GRPC_CHANNEL_READY,
                                       GRPC_CHANNEL_TRANSIENT_FAILURE))
              << ConnectivityStateName(from_state) << "=>"
              << ConnectivityStateName(to_state) << "\n"
              << location.file() << ":" << location.line();
          break;
        case GRPC_CHANNEL_READY:
          ASSERT_EQ(to_state, GRPC_CHANNEL_IDLE)
              << ConnectivityStateName(from_state) << "=>"
              << ConnectivityStateName(to_state) << "\n"
              << location.file() << ":" << location.line();
          break;
        case GRPC_CHANNEL_TRANSIENT_FAILURE:
          ASSERT_EQ(to_state, GRPC_CHANNEL_IDLE)
              << ConnectivityStateName(from_state) << "=>"
              << ConnectivityStateName(to_state) << "\n"
              << location.file() << ":" << location.line();
          break;
        default:
          FAIL() << ConnectivityStateName(from_state) << "=>"
                 << ConnectivityStateName(to_state) << "\n"
                 << location.file() << ":" << location.line();
          break;
      }
    }

    // Sets the connectivity state for this subchannel.  The updated state
    // will be reported to all associated SubchannelInterface objects.
    void SetConnectivityState(grpc_connectivity_state state,
                              const absl::Status& status = absl::OkStatus(),
                              SourceLocation location = SourceLocation()) {
      if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        EXPECT_FALSE(status.ok())
            << "bug in test: TRANSIENT_FAILURE must have non-OK status";
      } else {
        EXPECT_TRUE(status.ok())
            << "bug in test: " << ConnectivityStateName(state)
            << " must have OK status: " << status;
      }
      MutexLock lock(&mu_);
      AssertValidConnectivityStateTransition(state_tracker_.state(), state,
                                             location);
      state_tracker_.SetState(state, status, "set from test");
    }

    // Indicates if any of the associated SubchannelInterface objects
    // have requested a connection attempt since the last time this
    // method was called.
    bool ConnectionRequested() {
      MutexLock lock(&requested_connection_mu_);
      return std::exchange(requested_connection_, false);
    }

    // To be invoked by FakeHelper.
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        std::shared_ptr<WorkSerializer> work_serializer) {
      return MakeRefCounted<FakeSubchannel>(this, std::move(work_serializer));
    }

    // Sends an OOB backend metric report to all watchers.
    void SendOobBackendMetricReport(const BackendMetricData& backend_metrics) {
      MutexLock lock(&backend_metric_watcher_mu_);
      for (const auto* watcher : watchers_) {
        watcher->watcher()->OnBackendMetricReport(backend_metrics);
      }
    }

    // Checks that all OOB watchers have the expected reporting period.
    void CheckOobReportingPeriod(Duration expected,
                                 SourceLocation location = SourceLocation()) {
      MutexLock lock(&backend_metric_watcher_mu_);
      for (const auto* watcher : watchers_) {
        EXPECT_EQ(watcher->report_interval(), expected)
            << location.file() << ":" << location.line();
      }
    }

   private:
    const std::string address_;

    Mutex mu_;
    ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(&mu_);

    Mutex requested_connection_mu_;
    bool requested_connection_ ABSL_GUARDED_BY(&requested_connection_mu_) =
        false;

    Mutex backend_metric_watcher_mu_;
    std::set<OrcaWatcher*> watchers_
        ABSL_GUARDED_BY(&backend_metric_watcher_mu_);
  };

  // A fake helper to be passed to the LB policy.
  class FakeHelper : public LoadBalancingPolicy::ChannelControlHelper {
   public:
    // Represents a state update reported by the LB policy.
    struct StateUpdate {
      grpc_connectivity_state state;
      absl::Status status;
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker;

      std::string ToString() const {
        return absl::StrFormat("UPDATE{state=%s, status=%s, picker=%p}",
                               ConnectivityStateName(state), status.ToString(),
                               picker.get());
      }
    };

    // Represents a re-resolution request from the LB policy.
    struct ReresolutionRequested {
      std::string ToString() const { return "RERESOLUTION"; }
    };

    FakeHelper(LoadBalancingPolicyTest* test,
               std::shared_ptr<WorkSerializer> work_serializer,
               std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                   event_engine)
        : test_(test),
          work_serializer_(std::move(work_serializer)),
          event_engine_(std::move(event_engine)) {}

    bool QueueEmpty() {
      MutexLock lock(&mu_);
      return queue_.empty();
    }

    // Called at test tear-down time to ensure that we have not left any
    // unexpected events in the queue.
    void ExpectQueueEmpty(SourceLocation location = SourceLocation()) {
      MutexLock lock(&mu_);
      EXPECT_TRUE(queue_.empty())
          << location.file() << ":" << location.line() << "\n"
          << QueueString();
    }

    // Returns the next event in the queue if it is a state update.
    // If the queue is empty or the next event is not a state update,
    // fails the test and returns nullopt without removing anything from
    // the queue.
    absl::optional<StateUpdate> GetNextStateUpdate(
        SourceLocation location = SourceLocation()) {
      MutexLock lock(&mu_);
      EXPECT_FALSE(queue_.empty()) << location.file() << ":" << location.line();
      if (queue_.empty()) return absl::nullopt;
      Event& event = queue_.front();
      auto* update = absl::get_if<StateUpdate>(&event);
      EXPECT_NE(update, nullptr)
          << "unexpected event " << EventString(event) << " at "
          << location.file() << ":" << location.line();
      if (update == nullptr) return absl::nullopt;
      StateUpdate result = std::move(*update);
      gpr_log(GPR_INFO, "got next state update: %s", result.ToString().c_str());
      queue_.pop_front();
      return std::move(result);
    }

    // Returns the next event in the queue if it is a re-resolution.
    // If the queue is empty or the next event is not a re-resolution,
    // fails the test and returns nullopt without removing anything
    // from the queue.
    absl::optional<ReresolutionRequested> GetNextReresolution(
        SourceLocation location = SourceLocation()) {
      MutexLock lock(&mu_);
      EXPECT_FALSE(queue_.empty()) << location.file() << ":" << location.line();
      if (queue_.empty()) return absl::nullopt;
      Event& event = queue_.front();
      auto* reresolution = absl::get_if<ReresolutionRequested>(&event);
      EXPECT_NE(reresolution, nullptr)
          << "unexpected event " << EventString(event) << " at "
          << location.file() << ":" << location.line();
      if (reresolution == nullptr) return absl::nullopt;
      ReresolutionRequested result = *reresolution;
      queue_.pop_front();
      return result;
    }

   private:
    // Represents an event reported by the LB policy.
    using Event = absl::variant<StateUpdate, ReresolutionRequested>;

    // Returns a human-readable representation of an event.
    static std::string EventString(const Event& event) {
      return Match(
          event, [](const StateUpdate& update) { return update.ToString(); },
          [](const ReresolutionRequested& reresolution) {
            return reresolution.ToString();
          });
    }

    std::string QueueString() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
      std::vector<std::string> parts = {"Queue:"};
      for (const Event& event : queue_) {
        parts.push_back(EventString(event));
      }
      return absl::StrJoin(parts, "\n  ");
    }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override {
      SubchannelKey key(address.address(), args);
      auto it = test_->subchannel_pool_.find(key);
      if (it == test_->subchannel_pool_.end()) {
        auto address_uri = grpc_sockaddr_to_uri(&address.address());
        GPR_ASSERT(address_uri.ok());
        it = test_->subchannel_pool_
                 .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                          std::forward_as_tuple(std::move(*address_uri)))
                 .first;
      }
      return it->second.CreateSubchannel(work_serializer_);
    }

    void UpdateState(
        grpc_connectivity_state state, const absl::Status& status,
        RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
      MutexLock lock(&mu_);
      StateUpdate update{state, status, std::move(picker)};
      gpr_log(GPR_INFO, "state update from LB policy: %s",
              update.ToString().c_str());
      queue_.push_back(std::move(update));
    }

    void RequestReresolution() override {
      MutexLock lock(&mu_);
      queue_.push_back(ReresolutionRequested());
    }

    absl::string_view GetAuthority() override { return "server.example.com"; }

    grpc_event_engine::experimental::EventEngine* GetEventEngine() override {
      return event_engine_.get();
    }

    void AddTraceEvent(TraceSeverity, absl::string_view) override {}

    LoadBalancingPolicyTest* test_;
    std::shared_ptr<WorkSerializer> work_serializer_;
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;

    Mutex mu_;
    std::deque<Event> queue_ ABSL_GUARDED_BY(&mu_);
  };

  // A fake MetadataInterface implementation, for use in PickArgs.
  class FakeMetadata : public LoadBalancingPolicy::MetadataInterface {
   public:
    explicit FakeMetadata(std::map<std::string, std::string> metadata)
        : metadata_(std::move(metadata)) {}

    const std::map<std::string, std::string>& metadata() const {
      return metadata_;
    }

   private:
    void Add(absl::string_view key, absl::string_view value) override {
      metadata_[std::string(key)] = std::string(value);
    }

    std::vector<std::pair<std::string, std::string>> TestOnlyCopyToVector()
        override {
      return {};  // Not used.
    }

    absl::optional<absl::string_view> Lookup(
        absl::string_view key, std::string* /*buffer*/) const override {
      auto it = metadata_.find(std::string(key));
      if (it == metadata_.end()) return absl::nullopt;
      return it->second;
    }

    std::map<std::string, std::string> metadata_;
  };

  // A fake CallState implementation, for use in PickArgs.
  class FakeCallState : public ClientChannelLbCallState {
   public:
    explicit FakeCallState(const CallAttributes& attributes) {
      for (const auto& p : attributes) {
        attributes_.emplace(p->type(), p.get());
      }
    }

    ~FakeCallState() override {
      for (void* allocation : allocations_) {
        gpr_free(allocation);
      }
    }

   private:
    void* Alloc(size_t size) override {
      void* allocation = gpr_malloc(size);
      allocations_.push_back(allocation);
      return allocation;
    }

    ServiceConfigCallData::CallAttributeInterface* GetCallAttribute(
        UniqueTypeName type) const override {
      auto it = attributes_.find(type);
      if (it != attributes_.end()) {
        return it->second;
      }
      return nullptr;
    }

    std::vector<void*> allocations_;
    std::map<UniqueTypeName, ServiceConfigCallData::CallAttributeInterface*>
        attributes_;
  };

  // A fake BackendMetricAccessor implementation, for passing to
  // SubchannelCallTrackerInterface::Finish().
  class FakeBackendMetricAccessor
      : public LoadBalancingPolicy::BackendMetricAccessor {
   public:
    explicit FakeBackendMetricAccessor(
        absl::optional<BackendMetricData> backend_metric_data)
        : backend_metric_data_(std::move(backend_metric_data)) {}

    const BackendMetricData* GetBackendMetricData() override {
      if (backend_metric_data_.has_value()) return &*backend_metric_data_;
      return nullptr;
    }

   private:
    const absl::optional<BackendMetricData> backend_metric_data_;
  };

  LoadBalancingPolicyTest()
      : work_serializer_(std::make_shared<WorkSerializer>()) {}

  void TearDown() override {
    // Note: Can't safely trigger this from inside the FakeHelper dtor,
    // because if there is a picker in the queue that is holding a ref
    // to the LB policy, that will prevent the LB policy from being
    // destroyed, and therefore the FakeHelper will not be destroyed.
    // (This will cause an ASAN failure, but it will not display the
    // queued events, so the failure will be harder to diagnose.)
    helper_->ExpectQueueEmpty();
  }

  // Creates an LB policy of the specified name.
  // Creates a new FakeHelper for the new LB policy, and sets helper_ to
  // point to the FakeHelper.
  OrphanablePtr<LoadBalancingPolicy> MakeLbPolicy(absl::string_view name) {
    auto helper =
        std::make_unique<FakeHelper>(this, work_serializer_, event_engine_);
    helper_ = helper.get();
    LoadBalancingPolicy::Args args = {work_serializer_, std::move(helper),
                                      ChannelArgs()};
    return CoreConfiguration::Get()
        .lb_policy_registry()
        .CreateLoadBalancingPolicy(name, std::move(args));
  }

  // Creates an LB policy config from json.
  static RefCountedPtr<LoadBalancingPolicy::Config> MakeConfig(
      const Json& json, SourceLocation location = SourceLocation()) {
    auto status_or_config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            json);
    EXPECT_TRUE(status_or_config.ok())
        << status_or_config.status() << "\n"
        << location.file() << ":" << location.line();
    return status_or_config.value();
  }

  // Converts an address URI into a grpc_resolved_address.
  static grpc_resolved_address MakeAddress(absl::string_view address_uri) {
    auto uri = URI::Parse(address_uri);
    GPR_ASSERT(uri.ok());
    grpc_resolved_address address;
    GPR_ASSERT(grpc_parse_uri(*uri, &address));
    return address;
  }

  // Constructs an update containing a list of addresses.
  LoadBalancingPolicy::UpdateArgs BuildUpdate(
      absl::Span<const absl::string_view> addresses,
      RefCountedPtr<LoadBalancingPolicy::Config> config = nullptr) {
    LoadBalancingPolicy::UpdateArgs update;
    update.addresses.emplace();
    for (const absl::string_view& address : addresses) {
      update.addresses->emplace_back(MakeAddress(address), ChannelArgs());
    }
    update.config = std::move(config);
    return update;
  }

  // Applies the update on the LB policy.
  absl::Status ApplyUpdate(LoadBalancingPolicy::UpdateArgs update_args,
                           LoadBalancingPolicy* lb_policy) {
    absl::Status status;
    absl::Notification notification;
    work_serializer_->Run(
        [&]() {
          status = lb_policy->UpdateLocked(std::move(update_args));
          notification.Notify();
        },
        DEBUG_LOCATION);
    notification.WaitForNotification();
    return status;
  }

  void ExpectQueueEmpty(SourceLocation location = SourceLocation()) {
    helper_->ExpectQueueEmpty(location);
  }

  // Keeps reading state updates until continue_predicate() returns false.
  // Returns false if the helper reports no events or if the event is
  // not a state update; otherwise (if continue_predicate() tells us to
  // stop) returns true.
  bool WaitForStateUpdate(
      std::function<bool(FakeHelper::StateUpdate update)> continue_predicate,
      SourceLocation location = SourceLocation()) {
    gpr_log(GPR_INFO, "==> WaitForStateUpdate()");
    while (true) {
      auto update = helper_->GetNextStateUpdate(location);
      if (!update.has_value()) {
        gpr_log(GPR_INFO, "WaitForStateUpdate() returning false");
        return false;
      }
      if (!continue_predicate(std::move(*update))) {
        gpr_log(GPR_INFO, "WaitForStateUpdate() returning true");
        return true;
      }
    }
  }

  void ExpectReresolutionRequest(SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(helper_->GetNextReresolution(location))
        << location.file() << ":" << location.line();
  }

  // Expects that the LB policy has reported the specified connectivity
  // state to helper_.  Returns the picker from the state update.
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> ExpectState(
      grpc_connectivity_state expected_state,
      absl::Status expected_status = absl::OkStatus(),
      SourceLocation location = SourceLocation()) {
    auto update = helper_->GetNextStateUpdate(location);
    if (!update.has_value()) return nullptr;
    EXPECT_EQ(update->state, expected_state)
        << "got " << ConnectivityStateName(update->state) << ", expected "
        << ConnectivityStateName(expected_state) << "\n"
        << "at " << location.file() << ":" << location.line();
    EXPECT_EQ(update->status, expected_status)
        << update->status << "\n"
        << location.file() << ":" << location.line();
    EXPECT_NE(update->picker, nullptr)
        << location.file() << ":" << location.line();
    return std::move(update->picker);
  }

  // Waits for the LB policy to get connected, then returns the final
  // picker.  There can be any number of CONNECTING updates, each of
  // which must return a picker that queues picks, followed by one
  // update for state READY, whose picker is returned.
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> WaitForConnected(
      SourceLocation location = SourceLocation()) {
    gpr_log(GPR_INFO, "==> WaitForConnected()");
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> final_picker;
    WaitForStateUpdate(
        [&](FakeHelper::StateUpdate update) {
          if (update.state == GRPC_CHANNEL_CONNECTING) {
            EXPECT_TRUE(update.status.ok())
                << update.status << " at " << location.file() << ":"
                << location.line();
            ExpectPickQueued(update.picker.get(), {}, location);
            return true;  // Keep going.
          }
          EXPECT_EQ(update.state, GRPC_CHANNEL_READY)
              << ConnectivityStateName(update.state) << " at "
              << location.file() << ":" << location.line();
          final_picker = std::move(update.picker);
          return false;  // Stop.
        },
        location);
    return final_picker;
  }

  // Waits for the LB policy to fail a connection attempt.  There can be
  // any number of CONNECTING updates, each of which must return a picker
  // that queues picks, followed by one update for state TRANSIENT_FAILURE,
  // whose status is passed to check_status() and whose picker must fail
  // picks with a status that is passed to check_status().
  // Returns true if the reported states match expectations.
  bool WaitForConnectionFailed(
      std::function<void(const absl::Status&)> check_status,
      SourceLocation location = SourceLocation()) {
    bool retval = false;
    WaitForStateUpdate(
        [&](FakeHelper::StateUpdate update) {
          if (update.state == GRPC_CHANNEL_CONNECTING) {
            EXPECT_TRUE(update.status.ok())
                << update.status << " at " << location.file() << ":"
                << location.line();
            ExpectPickQueued(update.picker.get(), {}, location);
            return true;  // Keep going.
          }
          EXPECT_EQ(update.state, GRPC_CHANNEL_TRANSIENT_FAILURE)
              << ConnectivityStateName(update.state) << " at "
              << location.file() << ":" << location.line();
          check_status(update.status);
          ExpectPickFail(update.picker.get(), check_status, location);
          retval = update.state == GRPC_CHANNEL_TRANSIENT_FAILURE;
          return false;  // Stop.
        },
        location);
    return retval;
  }

  // Waits for the round_robin policy to start using an updated address list.
  // There can be any number of READY updates where the picker is still using
  // the old list followed by one READY update where the picker is using the
  // new list.  Returns a picker if the reported states match expectations.
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
  WaitForRoundRobinListChange(absl::Span<const absl::string_view> old_addresses,
                              absl::Span<const absl::string_view> new_addresses,
                              const CallAttributes& call_attributes = {},
                              size_t num_iterations = 3,
                              SourceLocation location = SourceLocation()) {
    gpr_log(GPR_INFO, "Waiting for expected RR addresses...");
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> retval;
    size_t num_picks =
        std::max(new_addresses.size(), old_addresses.size()) * num_iterations;
    WaitForStateUpdate(
        [&](FakeHelper::StateUpdate update) {
          EXPECT_EQ(update.state, GRPC_CHANNEL_READY)
              << location.file() << ":" << location.line();
          if (update.state != GRPC_CHANNEL_READY) return false;
          // Get enough picks to round-robin num_iterations times across all
          // expected addresses.
          auto picks = GetCompletePicks(update.picker.get(), num_picks,
                                        call_attributes, nullptr, location);
          EXPECT_TRUE(picks.has_value())
              << location.file() << ":" << location.line();
          if (!picks.has_value()) return false;
          gpr_log(GPR_INFO, "PICKS: %s", absl::StrJoin(*picks, " ").c_str());
          // If the picks still match the old list, then keep going.
          if (PicksAreRoundRobin(old_addresses, *picks)) return true;
          // Otherwise, the picks should match the new list.
          bool matches = PicksAreRoundRobin(new_addresses, *picks);
          EXPECT_TRUE(matches)
              << "Expected: " << absl::StrJoin(new_addresses, ", ")
              << "\nActual: " << absl::StrJoin(*picks, ", ") << "\nat "
              << location.file() << ":" << location.line();
          if (matches) {
            retval = std::move(update.picker);
          }
          return false;  // Stop.
        },
        location);
    return retval;
  }

  // Expects a state update for the specified state and status, and then
  // expects the resulting picker to queue picks.
  void ExpectStateAndQueuingPicker(
      grpc_connectivity_state expected_state,
      absl::Status expected_status = absl::OkStatus(),
      SourceLocation location = SourceLocation()) {
    auto picker = ExpectState(expected_state, expected_status, location);
    ExpectPickQueued(picker.get(), {}, location);
  }

  // Convenient frontend to ExpectStateAndQueuingPicker() for CONNECTING.
  void ExpectConnectingUpdate(SourceLocation location = SourceLocation()) {
    ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                                location);
  }

  static std::unique_ptr<LoadBalancingPolicy::MetadataInterface> MakeMetadata(
      std::map<std::string, std::string> init = {}) {
    return std::make_unique<FakeMetadata>(init);
  }

  // Does a pick and returns the result.
  LoadBalancingPolicy::PickResult DoPick(
      LoadBalancingPolicy::SubchannelPicker* picker,
      const CallAttributes& call_attributes = {}) {
    ExecCtx exec_ctx;
    FakeMetadata metadata({});
    FakeCallState call_state(call_attributes);
    return picker->Pick({"/service/method", &metadata, &call_state});
  }

  // Requests a pick on picker and expects a Queue result.
  void ExpectPickQueued(LoadBalancingPolicy::SubchannelPicker* picker,
                        const CallAttributes call_attributes = {},
                        SourceLocation location = SourceLocation()) {
    ASSERT_NE(picker, nullptr);
    auto pick_result = DoPick(picker, call_attributes);
    ASSERT_TRUE(absl::holds_alternative<LoadBalancingPolicy::PickResult::Queue>(
        pick_result.result))
        << PickResultString(pick_result) << "\nat " << location.file() << ":"
        << location.line();
  }

  // Requests a pick on picker and expects a Complete result.
  // The address of the resulting subchannel is returned, or nullopt if
  // the result was something other than Complete.
  // If the complete pick includes a SubchannelCallTrackerInterface, then if
  // subchannel_call_tracker is non-null, it will be set to point to the
  // call tracker; otherwise, the call tracker will be invoked
  // automatically to represent a complete call with no backend metric data.
  absl::optional<std::string> ExpectPickComplete(
      LoadBalancingPolicy::SubchannelPicker* picker,
      const CallAttributes& call_attributes = {},
      std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>*
          subchannel_call_tracker = nullptr,
      SourceLocation location = SourceLocation()) {
    EXPECT_NE(picker, nullptr);
    if (picker == nullptr) {
      return absl::nullopt;
    }
    auto pick_result = DoPick(picker, call_attributes);
    auto* complete = absl::get_if<LoadBalancingPolicy::PickResult::Complete>(
        &pick_result.result);
    EXPECT_NE(complete, nullptr) << PickResultString(pick_result) << " at "
                                 << location.file() << ":" << location.line();
    if (complete == nullptr) return absl::nullopt;
    auto* subchannel = static_cast<SubchannelState::FakeSubchannel*>(
        complete->subchannel.get());
    std::string address = subchannel->state()->address();
    if (complete->subchannel_call_tracker != nullptr) {
      if (subchannel_call_tracker != nullptr) {
        *subchannel_call_tracker = std::move(complete->subchannel_call_tracker);
      } else {
        complete->subchannel_call_tracker->Start();
        FakeMetadata metadata({});
        FakeBackendMetricAccessor backend_metric_accessor({});
        LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
            address, absl::OkStatus(), &metadata, &backend_metric_accessor};
        complete->subchannel_call_tracker->Finish(args);
      }
    }
    return address;
  }

  // Gets num_picks complete picks from picker and returns the resulting
  // list of addresses, or nullopt if a non-complete pick was returned.
  absl::optional<std::vector<std::string>> GetCompletePicks(
      LoadBalancingPolicy::SubchannelPicker* picker, size_t num_picks,
      const CallAttributes& call_attributes = {},
      std::vector<
          std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>>*
          subchannel_call_trackers = nullptr,
      SourceLocation location = SourceLocation()) {
    EXPECT_NE(picker, nullptr);
    if (picker == nullptr) {
      return absl::nullopt;
    }
    std::vector<std::string> results;
    for (size_t i = 0; i < num_picks; ++i) {
      std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
          subchannel_call_tracker;
      auto address = ExpectPickComplete(picker, call_attributes,
                                        subchannel_call_trackers == nullptr
                                            ? nullptr
                                            : &subchannel_call_tracker,
                                        location);
      if (!address.has_value()) return absl::nullopt;
      results.emplace_back(std::move(*address));
      if (subchannel_call_trackers != nullptr) {
        subchannel_call_trackers->emplace_back(
            std::move(subchannel_call_tracker));
      }
    }
    return results;
  }

  // Returns true if the list of actual pick result addresses matches the
  // list of expected addresses for round_robin.  Note that the actual
  // addresses may start anywhere in the list of expected addresses but
  // must then continue in round-robin fashion, with wrap-around.
  bool PicksAreRoundRobin(absl::Span<const absl::string_view> expected,
                          absl::Span<const std::string> actual) {
    absl::optional<size_t> expected_index;
    for (const auto& address : actual) {
      auto it = std::find(expected.begin(), expected.end(), address);
      if (it == expected.end()) return false;
      size_t index = it - expected.begin();
      if (expected_index.has_value() && index != *expected_index) return false;
      expected_index = (index + 1) % expected.size();
    }
    return true;
  }

  // Checks that the picker has round-robin behavior over the specified
  // set of addresses.
  void ExpectRoundRobinPicks(LoadBalancingPolicy::SubchannelPicker* picker,
                             absl::Span<const absl::string_view> addresses,
                             const CallAttributes& call_attributes = {},
                             size_t num_iterations = 3,
                             SourceLocation location = SourceLocation()) {
    auto picks = GetCompletePicks(picker, num_iterations * addresses.size(),
                                  call_attributes, nullptr, location);
    ASSERT_TRUE(picks.has_value()) << location.file() << ":" << location.line();
    EXPECT_TRUE(PicksAreRoundRobin(addresses, *picks))
        << "  Actual: " << absl::StrJoin(*picks, ", ")
        << "\n  Expected: " << absl::StrJoin(addresses, ", ") << "\n"
        << location.file() << ":" << location.line();
  }

  // Expect startup with RR with a set of addresses.
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> ExpectRoundRobinStartup(
      absl::Span<const absl::string_view> addresses) {
    ExpectConnectingUpdate();
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker;
    for (size_t i = 0; i < addresses.size(); ++i) {
      auto* subchannel = FindSubchannel(addresses[i]);
      EXPECT_NE(subchannel, nullptr);
      if (subchannel == nullptr) return nullptr;
      EXPECT_TRUE(subchannel->ConnectionRequested());
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
      subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
      if (i == 0) {
        picker = WaitForConnected();
        ExpectRoundRobinPicks(picker.get(), {addresses[0]});
      } else {
        picker = WaitForRoundRobinListChange(
            absl::MakeSpan(addresses).subspan(0, i),
            absl::MakeSpan(addresses).subspan(0, i + 1));
      }
    }
    return picker;
  }

  // Requests a picker on picker and expects a Fail result.
  // The failing status is passed to check_status.
  void ExpectPickFail(LoadBalancingPolicy::SubchannelPicker* picker,
                      std::function<void(const absl::Status&)> check_status,
                      SourceLocation location = SourceLocation()) {
    auto pick_result = DoPick(picker);
    auto* fail = absl::get_if<LoadBalancingPolicy::PickResult::Fail>(
        &pick_result.result);
    ASSERT_NE(fail, nullptr) << PickResultString(pick_result) << " at "
                             << location.file() << ":" << location.line();
    check_status(fail->status);
  }

  // Returns a human-readable string for a pick result.
  static std::string PickResultString(
      const LoadBalancingPolicy::PickResult& result) {
    return Match(
        result.result,
        [](const LoadBalancingPolicy::PickResult::Complete& complete) {
          auto* subchannel = static_cast<SubchannelState::FakeSubchannel*>(
              complete.subchannel.get());
          return absl::StrFormat(
              "COMPLETE{subchannel=%s, subchannel_call_tracker=%p}",
              subchannel->state()->address(),
              complete.subchannel_call_tracker.get());
        },
        [](const LoadBalancingPolicy::PickResult::Queue&) -> std::string {
          return "QUEUE{}";
        },
        [](const LoadBalancingPolicy::PickResult::Fail& fail) -> std::string {
          return absl::StrFormat("FAIL{%s}", fail.status.ToString());
        },
        [](const LoadBalancingPolicy::PickResult::Drop& drop) -> std::string {
          return absl::StrFormat("FAIL{%s}", drop.status.ToString());
        });
  }

  // Returns the entry in the subchannel pool, or null if not present.
  SubchannelState* FindSubchannel(absl::string_view address,
                                  const ChannelArgs& args = ChannelArgs()) {
    SubchannelKey key(MakeAddress(address), args);
    auto it = subchannel_pool_.find(key);
    if (it == subchannel_pool_.end()) return nullptr;
    return &it->second;
  }

  // Creates and returns an entry in the subchannel pool.
  // This can be used in cases where we want to test that a subchannel
  // already exists when the LB policy creates it (e.g., due to it being
  // created by another channel and shared via the global subchannel
  // pool, or by being created by another LB policy in this channel).
  SubchannelState* CreateSubchannel(absl::string_view address,
                                    const ChannelArgs& args = ChannelArgs()) {
    SubchannelKey key(MakeAddress(address), args);
    auto it = subchannel_pool_
                  .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                           std::forward_as_tuple(address))
                  .first;
    return &it->second;
  }

  std::shared_ptr<WorkSerializer> work_serializer_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  FakeHelper* helper_ = nullptr;
  std::map<SubchannelKey, SubchannelState> subchannel_pool_;
};

// A subclass to be used for LB policies that start timers.
// Injects a mock EventEngine and provides the necessary framework for
// incrementing time and handling timer callbacks.
class TimeAwareLoadBalancingPolicyTest : public LoadBalancingPolicyTest {
 protected:
  // A custom time cache for which InvalidateCache() is a no-op.  This
  // ensures that when the timer callback instantiates its own ExecCtx
  // and therefore its own ScopedTimeCache, it continues to see the time
  // that we are injecting in the test.
  class TestTimeCache final : public Timestamp::ScopedSource {
   public:
    TestTimeCache() : cached_time_(previous()->Now()) {}

    Timestamp Now() override { return cached_time_; }
    void InvalidateCache() override {}

    void IncrementBy(Duration duration) { cached_time_ += duration; }

   private:
    Timestamp cached_time_;
  };

  TimeAwareLoadBalancingPolicyTest() {
    auto mock_ee =
        std::make_shared<grpc_event_engine::experimental::MockEventEngine>();
    auto capture = [this](std::chrono::duration<int64_t, std::nano> duration,
                          absl::AnyInvocable<void()> callback) {
      CheckExpectedTimerDuration(duration);
      intptr_t key = next_key_++;
      timer_callbacks_[key] = std::move(callback);
      return grpc_event_engine::experimental::EventEngine::TaskHandle{key, 0};
    };
    ON_CALL(*mock_ee,
            RunAfter(::testing::_, ::testing::A<absl::AnyInvocable<void()>>()))
        .WillByDefault(capture);
    auto cancel =
        [this](
            grpc_event_engine::experimental::EventEngine::TaskHandle handle) {
          auto it = timer_callbacks_.find(handle.keys[0]);
          if (it == timer_callbacks_.end()) return false;
          timer_callbacks_.erase(it);
          return true;
        };
    ON_CALL(*mock_ee, Cancel(::testing::_)).WillByDefault(cancel);
    // Store in base class, to make it visible to the LB policy.
    event_engine_ = std::move(mock_ee);
  }

  ~TimeAwareLoadBalancingPolicyTest() override {
    EXPECT_TRUE(timer_callbacks_.empty())
        << "WARNING: Test did not run all timer callbacks";
  }

  void RunTimerCallback() {
    ASSERT_EQ(timer_callbacks_.size(), 1UL);
    auto it = timer_callbacks_.begin();
    ASSERT_NE(it->second, nullptr);
    std::move(it->second)();
    timer_callbacks_.erase(it);
  }

  // Called when the LB policy starts a timer.
  // May be overridden by subclasses.
  virtual void CheckExpectedTimerDuration(
      grpc_event_engine::experimental::EventEngine::Duration) {}

  std::map<intptr_t, absl::AnyInvocable<void()>> timer_callbacks_;
  intptr_t next_key_ = 1;
  TestTimeCache time_cache_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_CLIENT_CHANNEL_LB_POLICY_LB_POLICY_TEST_LIB_H
