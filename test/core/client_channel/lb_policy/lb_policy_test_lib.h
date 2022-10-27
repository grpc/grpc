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

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_LB_POLICY_LB_POLICY_TEST_LIB_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_LB_POLICY_LB_POLICY_TEST_LIB_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace testing {

class LoadBalancingPolicyTest : public ::testing::Test {
 protected:
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
                     const grpc_resolved_address& address,
                     const ChannelArgs& args,
                     std::shared_ptr<WorkSerializer> work_serializer)
          : state_(state),
            address_(address),
            args_(args),
            work_serializer_(std::move(work_serializer)) {}

      const grpc_resolved_address& address() const { return address_; }

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
        MutexLock lock(&state_->mu_);
        state_->requested_connection_ = true;
      }

      // Don't need these methods here, so they're no-ops.
      void ResetBackoff() override {}
      void AddDataWatcher(std::unique_ptr<DataWatcherInterface>) override {}

      SubchannelState* state_;
      grpc_resolved_address address_;
      ChannelArgs args_;
      std::shared_ptr<WorkSerializer> work_serializer_;
      std::map<SubchannelInterface::ConnectivityStateWatcherInterface*,
               WatcherWrapper*>
          watcher_map_;
    };

    SubchannelState() : state_tracker_("LoadBalancingPolicyTest") {}

    // Sets the connectivity state for this subchannel.  The updated state
    // will be reported to all associated SubchannelInterface objects.
    void SetConnectivityState(grpc_connectivity_state state,
                              const absl::Status& status) {
      MutexLock lock(&mu_);
      state_tracker_.SetState(state, status, "set from test");
    }

    // Indicates if any of the associated SubchannelInterface objects
    // have requested a connection attempt since the last time this
    // method was called.
    bool ConnectionRequested() {
      MutexLock lock(&mu_);
      return std::exchange(requested_connection_, false);
    }

    // To be invoked by FakeHelper.
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address, const ChannelArgs& args,
        std::shared_ptr<WorkSerializer> work_serializer) {
      return MakeRefCounted<FakeSubchannel>(this, address, args,
                                            std::move(work_serializer));
    }

   private:
    Mutex mu_;
    bool requested_connection_ ABSL_GUARDED_BY(&mu_) = false;
    ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(&mu_);
  };

  // A fake helper to be passed to the LB policy.
  class FakeHelper : public LoadBalancingPolicy::ChannelControlHelper {
   public:
    // Represents a state update reported by the LB policy.
    struct StateUpdate {
      grpc_connectivity_state state;
      absl::Status status;
      std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker;
    };

    // Represents a re-resolution request from the LB policy.
    struct ReresolutionRequested {};

    // Represents an event reported by the LB policy.
    using Event = absl::variant<StateUpdate, ReresolutionRequested>;

    FakeHelper(LoadBalancingPolicyTest* test,
               std::shared_ptr<WorkSerializer> work_serializer)
        : test_(test), work_serializer_(std::move(work_serializer)) {}

    // Returns the most recent event from the LB policy, or nullopt if
    // there have been no events.
    absl::optional<Event> GetEvent() {
      MutexLock lock(&mu_);
      if (queue_.empty()) return absl::nullopt;
      Event event = std::move(queue_.front());
      queue_.pop_front();
      return std::move(event);
    }

   private:
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override {
      SubchannelKey key(address.address(), args);
      auto& subchannel_state = test_->subchannel_pool_[key];
      return subchannel_state.CreateSubchannel(address.address(), args,
                                               work_serializer_);
    }

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<LoadBalancingPolicy::SubchannelPicker>
                         picker) override {
      MutexLock lock(&mu_);
      queue_.push_back(StateUpdate{state, status, std::move(picker)});
    }

    void RequestReresolution() override {
      MutexLock lock(&mu_);
      queue_.push_back(ReresolutionRequested());
    }

    absl::string_view GetAuthority() override { return "server.example.com"; }

    grpc_event_engine::experimental::EventEngine* GetEventEngine() override {
      return grpc_event_engine::experimental::GetDefaultEventEngine().get();
    }

    void AddTraceEvent(TraceSeverity, absl::string_view) override {}

    LoadBalancingPolicyTest* test_;
    std::shared_ptr<WorkSerializer> work_serializer_;
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
  class FakeCallState : public LoadBalancingPolicy::CallState {
   public:
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

    std::vector<void*> allocations_;
  };

  LoadBalancingPolicyTest()
      : work_serializer_(std::make_shared<WorkSerializer>()) {}

  // Creates an LB policy of the specified name.
  // Creates a new FakeHelper for the new LB policy, and sets helper_ to
  // point to the FakeHelper.
  OrphanablePtr<LoadBalancingPolicy> MakeLbPolicy(absl::string_view name) {
    auto helper = std::make_unique<FakeHelper>(this, work_serializer_);
    helper_ = helper.get();
    LoadBalancingPolicy::Args args = {work_serializer_, std::move(helper),
                                      ChannelArgs()};
    return CoreConfiguration::Get()
        .lb_policy_registry()
        .CreateLoadBalancingPolicy(name, std::move(args));
  }

  // Creates an LB policy config from json.
  static RefCountedPtr<LoadBalancingPolicy::Config> MakeConfig(
      const Json& json) {
    return CoreConfiguration::Get()
        .lb_policy_registry()
        .ParseLoadBalancingConfig(json)
        .value();
  }

  // Converts an address URI into a grpc_resolved_address.
  static grpc_resolved_address MakeAddress(absl::string_view address_uri) {
    auto uri = URI::Parse(address_uri);
    GPR_ASSERT(uri.ok());
    grpc_resolved_address address;
    GPR_ASSERT(grpc_parse_uri(*uri, &address));
    return address;
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

  // Keeps reading state updates until continue_predicate() returns false.
  // Returns false if the helper reports no events or if the event is
  // not a state update; otherwise (if continue_predicate() tells us to
  // stop) returns true.
  bool WaitForStateUpdate(
      std::function<
          bool(grpc_connectivity_state, absl::Status,
               std::unique_ptr<LoadBalancingPolicy::SubchannelPicker>)>
          continue_predicate,
      SourceLocation location = SourceLocation()) {
    while (true) {
      auto event = helper_->GetEvent();
      EXPECT_TRUE(event.has_value())
          << location.file() << ":" << location.line();
      if (!event.has_value()) return false;
      auto* update = absl::get_if<FakeHelper::StateUpdate>(&*event);
      EXPECT_NE(update, nullptr) << location.file() << ":" << location.line();
      if (update == nullptr) return false;
      if (!continue_predicate(update->state, std::move(update->status),
                              std::move(update->picker))) {
        return true;
      }
    }
  }

  // Expects that the LB policy has reported the specified connectivity
  // state to helper_.  Returns the picker from the state update.
  std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> ExpectState(
      grpc_connectivity_state expected_state,
      absl::Status expected_status = absl::OkStatus(),
      SourceLocation location = SourceLocation()) {
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> final_picker;
    WaitForStateUpdate(
        [&](grpc_connectivity_state state, absl::Status status,
            std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker) {
          EXPECT_EQ(state, expected_state)
              << "got " << ConnectivityStateName(state) << ", expected "
              << ConnectivityStateName(expected_state) << "\n"
              << "at " << location.file() << ":" << location.line();
          EXPECT_EQ(status, expected_status)
              << status << "\n"
              << location.file() << ":" << location.line();
          EXPECT_NE(picker, nullptr)
              << location.file() << ":" << location.line();
          final_picker = std::move(picker);
          return false;
        });
    return final_picker;
  }

  // Waits for the LB policy to get connected.
  std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> WaitForConnected(
      SourceLocation location = SourceLocation()) {
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> final_picker;
    WaitForStateUpdate(
        [&](grpc_connectivity_state state, absl::Status status,
            std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker) {
          if (state == GRPC_CHANNEL_CONNECTING) {
            EXPECT_TRUE(status.ok()) << status;
            ExpectPickQueued(picker.get(), location);
            return true;
          }
          EXPECT_EQ(state, GRPC_CHANNEL_READY) << ConnectivityStateName(state);
          final_picker = std::move(picker);
          return false;
        });
    return final_picker;
  }

  // Requests a pick on picker and expects a Queue result.
  void ExpectPickQueued(LoadBalancingPolicy::SubchannelPicker* picker,
                        SourceLocation location = SourceLocation()) {
    ExecCtx exec_ctx;
    FakeMetadata metadata({});
    FakeCallState call_state;
    auto pick_result =
        picker->Pick({"/service/method", &metadata, &call_state});
    ASSERT_TRUE(absl::holds_alternative<LoadBalancingPolicy::PickResult::Queue>(
        pick_result.result))
        << location.file() << ":" << location.line();
  }

  // Requests a pick on picker and expects a Complete result whose
  // subchannel has the specified address.
  void ExpectPickComplete(LoadBalancingPolicy::SubchannelPicker* picker,
                          absl::string_view address_uri,
                          SourceLocation location = SourceLocation()) {
    ExecCtx exec_ctx;
    FakeMetadata metadata({});
    FakeCallState call_state;
    auto pick_result =
        picker->Pick({"/service/method", &metadata, &call_state});
    auto* complete = absl::get_if<LoadBalancingPolicy::PickResult::Complete>(
        &pick_result.result);
    ASSERT_NE(complete, nullptr) << location.file() << ":" << location.line();
    auto* subchannel = static_cast<SubchannelState::FakeSubchannel*>(
        complete->subchannel.get());
    auto uri = grpc_sockaddr_to_uri(&subchannel->address());
    ASSERT_TRUE(uri.ok()) << uri.status() << " at " << location.file() << ":"
                          << location.line();
    EXPECT_EQ(*uri, address_uri) << location.file() << ":" << location.line();
  }

  std::shared_ptr<WorkSerializer> work_serializer_;
  FakeHelper* helper_ = nullptr;
  std::map<SubchannelKey, SubchannelState> subchannel_pool_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_CLIENT_CHANNEL_LB_POLICY_LB_POLICY_TEST_LIB_H
