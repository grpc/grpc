//
// Copyright 2025 gRPC authors.
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

#include <google/protobuf/text_format.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpcpp/impl/codegen/config_protobuf.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/subchannel_interface_internal.h"
#include "src/core/client_channel/subchannel_pool_interface.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/health_check_client_internal.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/match.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/core/util/work_serializer.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/load_balancing/pick_first_fuzzer.pb.h"
#include "test/core/test_util/fuzzing_channel_args.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;

namespace grpc_core {
namespace testing {

// TODO(roth): Refactor to avoid duplication with lb_policy_test_lib.h.
// TODO(roth): Make this a general-purpose framework that can be applied
// to any LB policy (or even across all LB policies).
class Fuzzer {
 public:
  explicit Fuzzer(const fuzzing_event_engine::Actions& fuzzing_ee_actions) {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_ee_actions);
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    work_serializer_ = std::make_shared<WorkSerializer>(event_engine_);
  }

  ~Fuzzer() {
    lb_policy_.reset();
    work_serializer_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  void Act(const pick_first_fuzzer::Action& action) {
    std::string action_text;
    if (!google::protobuf::TextFormat::PrintToString(action, &action_text)) {
      action_text = "<unknown>";
    }
    LOG(INFO) << "Action: " << action_text;
    switch (action.action_type_case()) {
      case pick_first_fuzzer::Action::kUpdate:
        Update(action.update());
        break;
      case pick_first_fuzzer::Action::kSubchannelConnectivityNotification:
        SubchannelConnectivityNotification(
            action.subchannel_connectivity_notification());
        break;
      case pick_first_fuzzer::Action::kExitIdle:
        ExitIdle();
        break;
      case pick_first_fuzzer::Action::kResetBackoff:
        ResetBackoff();
        break;
      case pick_first_fuzzer::Action::kCreateLbPolicy:
        CreateLbPolicy(action.create_lb_policy());
        break;
      case pick_first_fuzzer::Action::kDoPick:
        DoPick();
        break;
      case pick_first_fuzzer::Action::kTick:
        event_engine_->TickForDuration(Duration::Milliseconds(
            // Cap to 10 hours.
            std::min<uint64_t>(action.tick().ms(), 36000000)));
        break;
      case pick_first_fuzzer::Action::ACTION_TYPE_NOT_SET:
        break;
    }
    // When the LB policy is reporting TF state, we should always be trying
    // to connect to at least one subchannel, if there are any not in state
    // TF.  Note that we check this only if we've received a new picker since
    // the last update we sent to the LB policy, since the check would fail
    // in the case where the LB policy previously had an empty address list
    // and was sent a non-empty list but had not yet had a chance to trigger
    // a connection attempt on any subchannels.
    if (got_picker_since_last_update_ &&
        state_ == GRPC_CHANNEL_TRANSIENT_FAILURE &&
        num_subchannels_ > num_subchannels_transient_failure_) {
      ASSERT_GT(num_subchannels_connecting_, 0);
    }
  }

  void CheckCanBecomeReady() {
    if (lb_policy_ == nullptr) return;
    if (state_ == GRPC_CHANNEL_READY) return;
    LOG(INFO) << "Checking that the policy can become READY";
    ExecCtx exec_ctx;
    // If the last update didn't contain any addresses, send an update
    // with one address.
    if (last_update_num_endpoints_ == 0) {
      LOG(INFO) << "Last update has no endpoints; sending new update";
      LoadBalancingPolicy::UpdateArgs update_args;
      update_args.config = MakeLbConfig("{}").value();
      update_args.addresses =
          std::make_shared<SingleEndpointIterator>(EndpointAddresses(
              MakeAddress("ipv4:127.0.0.1:1024").value(), ChannelArgs()));
      absl::Status status = lb_policy_->UpdateLocked(std::move(update_args));
      LOG(INFO) << "UpdateLocked() returned status: " << status;
    }
    // Drain any subchannel connectivity state notifications that may be
    // in the WorkSerializer queue.
    event_engine_->TickUntilIdle();
    // If LB policy is IDLE, trigger it to start connecting.
    if (state_ == GRPC_CHANNEL_IDLE) lb_policy_->ExitIdleLocked();
    // Find the first entry in the subchannel pool that actually has a
    // subchannel.
    SubchannelState* subchannel = nullptr;
    for (auto& [_, subchannel_state] : subchannel_pool_) {
      if (subchannel_state.num_subchannels() > 0) {
        subchannel = &subchannel_state;
        break;
      }
    }
    CHECK_NE(subchannel, nullptr);
    // Advance the subchannel through the connectivity states until it
    // gets to READY.
    LOG(INFO) << "Found subchannel for " << subchannel->address()
              << ", current state is "
              << ConnectivityStateName(subchannel->connectivity_state());
    switch (subchannel->connectivity_state()) {
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        LOG(INFO) << "Advancing state to IDLE";
        subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
        event_engine_->TickUntilIdle();
        [[fallthrough]];
      case GRPC_CHANNEL_IDLE:
        LOG(INFO) << "Advancing state to CONNECTING";
        subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
        event_engine_->TickUntilIdle();
        [[fallthrough]];
      case GRPC_CHANNEL_CONNECTING:
        LOG(INFO) << "Advancing state to READY";
        subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
        event_engine_->TickUntilIdle();
        [[fallthrough]];
      case GRPC_CHANNEL_READY:
      default:
        break;
    }
    // Make sure the LB policy is now reporting READY state.
    ASSERT_EQ(state_, GRPC_CHANNEL_READY);
    // Make sure the picker is returning the selected subchannel.
    LOG(INFO) << "Checking pick result";
    FakeMetadata md({});
    FakeCallState call_state({});
    auto result = picker_->Pick({"/service/method", &md, &call_state});
    Match(
        result.result,
        [&](const LoadBalancingPolicy::PickResult::Complete& complete) {
          EXPECT_EQ(complete.subchannel->address(), subchannel->address());
        },
        [&](const LoadBalancingPolicy::PickResult::Queue&) {
          FAIL() << "Pick returned Queue";
        },
        [&](const LoadBalancingPolicy::PickResult::Fail& fail) {
          FAIL() << "Pick returned Fail: " << fail.status;
        },
        [&](const LoadBalancingPolicy::PickResult::Drop& drop) {
          FAIL() << "Pick returned Drop: " << drop.status;
        });
  }

 private:
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
      explicit FakeSubchannel(SubchannelState* state) : state_(state) {}

      ~FakeSubchannel() override {
        for (const auto& [_, watcher] : watcher_map_) {
          state_->state_tracker_.RemoveWatcher(watcher);
        }
        state_->SubchannelDestroyed();
      }

     private:
      // Converts between
      // SubchannelInterface::ConnectivityStateWatcherInterface and
      // ConnectivityStateWatcherInterface.
      //
      // We support both unique_ptr<> and shared_ptr<>, since raw
      // connectivity watches use the latter but health watches use the
      // former.
      // TODO(roth): Clean this up.
      class WatcherWrapper : public AsyncConnectivityStateWatcherInterface {
       public:
        WatcherWrapper(
            Fuzzer* fuzzer, std::shared_ptr<WorkSerializer> work_serializer,
            std::unique_ptr<
                SubchannelInterface::ConnectivityStateWatcherInterface>
                watcher)
            : AsyncConnectivityStateWatcherInterface(
                  std::move(work_serializer)),
              fuzzer_(fuzzer),
              watcher_(std::move(watcher)) {}

        WatcherWrapper(
            Fuzzer* fuzzer, std::shared_ptr<WorkSerializer> work_serializer,
            std::shared_ptr<
                SubchannelInterface::ConnectivityStateWatcherInterface>
                watcher)
            : AsyncConnectivityStateWatcherInterface(
                  std::move(work_serializer)),
              fuzzer_(fuzzer),
              watcher_(std::move(watcher)) {}

        ~WatcherWrapper() override {
          if (current_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) {
            --fuzzer_->num_subchannels_transient_failure_;
          }
        }

        void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                       const absl::Status& status) override {
          LOG(INFO) << "notifying watcher: state="
                    << ConnectivityStateName(new_state) << " status=" << status;
          if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
            ++fuzzer_->num_subchannels_transient_failure_;
          } else if (current_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) {
            --fuzzer_->num_subchannels_transient_failure_;
          }
          current_state_ = new_state;
          watcher_->OnConnectivityStateChange(new_state, status);
        }

       private:
        Fuzzer* fuzzer_;
        std::shared_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
            watcher_;
        std::optional<grpc_connectivity_state> current_state_;
      };

      std::string address() const override { return state_->address_; }

      void WatchConnectivityState(
          std::unique_ptr<
              SubchannelInterface::ConnectivityStateWatcherInterface>
              watcher) override {
        auto* watcher_ptr = watcher.get();
        auto watcher_wrapper = MakeOrphanable<WatcherWrapper>(
            state_->fuzzer_, state_->work_serializer(), std::move(watcher));
        watcher_map_[watcher_ptr] = watcher_wrapper.get();
        state_->state_tracker_.AddWatcher(GRPC_CHANNEL_SHUTDOWN,
                                          std::move(watcher_wrapper));
      }

      void CancelConnectivityStateWatch(
          ConnectivityStateWatcherInterface* watcher) override {
        auto it = watcher_map_.find(watcher);
        if (it == watcher_map_.end()) return;
        state_->state_tracker_.RemoveWatcher(it->second);
        watcher_map_.erase(it);
      }

      void RequestConnection() override { state_->ConnectionRequested(); }

      void AddDataWatcher(
          std::unique_ptr<DataWatcherInterface> watcher) override {
        auto* w =
            static_cast<InternalSubchannelDataWatcherInterface*>(watcher.get());
        if (w->type() == HealthProducer::Type()) {
          // TODO(roth): Support health checking in test framework.
          // For now, we just hard-code this to the raw connectivity state.
          CHECK(health_watcher_ == nullptr);
          CHECK_EQ(health_watcher_wrapper_, nullptr);
          health_watcher_.reset(static_cast<HealthWatcher*>(watcher.release()));
          auto connectivity_watcher = health_watcher_->TakeWatcher();
          auto* connectivity_watcher_ptr = connectivity_watcher.get();
          auto watcher_wrapper = MakeOrphanable<WatcherWrapper>(
              state_->fuzzer_, state_->work_serializer(),
              std::move(connectivity_watcher));
          health_watcher_wrapper_ = watcher_wrapper.get();
          state_->state_tracker_.AddWatcher(GRPC_CHANNEL_SHUTDOWN,
                                            std::move(watcher_wrapper));
          LOG(INFO) << "AddDataWatcher(): added HealthWatch="
                    << health_watcher_.get()
                    << " connectivity_watcher=" << connectivity_watcher_ptr
                    << " watcher_wrapper=" << health_watcher_wrapper_;
        }
      }

      void CancelDataWatcher(DataWatcherInterface* watcher) override {
        auto* w = static_cast<InternalSubchannelDataWatcherInterface*>(watcher);
        if (w->type() == HealthProducer::Type()) {
          if (health_watcher_.get() != static_cast<HealthWatcher*>(watcher)) {
            return;
          }
          LOG(INFO) << "CancelDataWatcher(): cancelling HealthWatch="
                    << health_watcher_.get()
                    << " watcher_wrapper=" << health_watcher_wrapper_;
          state_->state_tracker_.RemoveWatcher(health_watcher_wrapper_);
          health_watcher_wrapper_ = nullptr;
          health_watcher_.reset();
        }
      }

      // Don't need this method, so it's a no-op.
      void ResetBackoff() override {}

      SubchannelState* state_;
      std::map<SubchannelInterface::ConnectivityStateWatcherInterface*,
               WatcherWrapper*>
          watcher_map_;
      std::unique_ptr<HealthWatcher> health_watcher_;
      WatcherWrapper* health_watcher_wrapper_ = nullptr;
    };

    SubchannelState(absl::string_view address, Fuzzer* fuzzer)
        : address_(address), fuzzer_(fuzzer), state_tracker_("Fuzzer") {}

    const std::string& address() const { return address_; }

    std::shared_ptr<WorkSerializer> work_serializer() const {
      return fuzzer_->work_serializer_;
    }

    grpc_connectivity_state connectivity_state() const {
      return state_tracker_.state();
    }

    uint64_t num_subchannels() const { return num_subchannels_; }

    // Sets the connectivity state for this subchannel.  The updated state
    // will be reported to all associated SubchannelInterface objects.
    void SetConnectivityState(grpc_connectivity_state state,
                              absl::Status status = absl::OkStatus()) {
      if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        if (status.ok()) {
          status = absl::UnavailableError("connection attempt failed");
        }
      } else if (!status.ok()) {
        status = absl::OkStatus();
      }
      if (state_tracker_.state() == GRPC_CHANNEL_CONNECTING) {
        ConnectionAttemptComplete();
      }
      // Updating the state in the state tracker will enqueue
      // notifications to watchers on the WorkSerializer.
      ExecCtx exec_ctx;
      state_tracker_.SetState(state, status, "set from test");
    }

    // To be invoked by FakeHelper.
    RefCountedPtr<SubchannelInterface> CreateSubchannel() {
      ++fuzzer_->num_subchannels_;
      ++num_subchannels_;
      return MakeRefCounted<FakeSubchannel>(this);
    }

   private:
    void SubchannelDestroyed() {
      --fuzzer_->num_subchannels_;
      --num_subchannels_;
      if (num_subchannels_ == 0) {
        state_tracker_.SetState(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                                "all subchannels destroyed");
        ConnectionAttemptComplete();
      }
    }

    void ConnectionRequested() {
      if (connection_requested_) return;
      connection_requested_ = true;
      ++fuzzer_->num_subchannels_connecting_;
    }

    void ConnectionAttemptComplete() {
      if (!connection_requested_) return;
      connection_requested_ = false;
      --fuzzer_->num_subchannels_connecting_;
    }

    const std::string address_;
    Fuzzer* const fuzzer_;
    ConnectivityStateTracker state_tracker_;
    uint64_t num_subchannels_ = 0;
    bool connection_requested_ = false;
  };

  // A fake helper to be passed to the LB policy.
  class FakeHelper : public LoadBalancingPolicy::ChannelControlHelper {
   public:
    explicit FakeHelper(Fuzzer* fuzzer) : fuzzer_(fuzzer) {}

   private:
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& /*per_address_args*/,
        const ChannelArgs& args) override {
      auto address_uri = grpc_sockaddr_to_uri(&address);
      if (!address_uri.ok()) return nullptr;
      // TODO(roth): Need to use per_address_args here.
      SubchannelKey key(
          address, args.RemoveAllKeysWithPrefix(GRPC_ARG_NO_SUBCHANNEL_PREFIX));
      auto it = fuzzer_->subchannel_pool_.find(key);
      if (it == fuzzer_->subchannel_pool_.end()) {
        it = fuzzer_->subchannel_pool_
                 .emplace(
                     std::piecewise_construct, std::forward_as_tuple(key),
                     std::forward_as_tuple(std::move(*address_uri), fuzzer_))
                 .first;
      }
      return it->second.CreateSubchannel();
    }

    void UpdateState(
        grpc_connectivity_state state, const absl::Status& status,
        RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
      LOG(INFO) << "LB policy called UpdateState("
                << ConnectivityStateName(state) << ", " << status << ")";
      fuzzer_->state_ = state;
      fuzzer_->status_ = status;
      fuzzer_->picker_ = std::move(picker);
      fuzzer_->got_picker_since_last_update_ = true;
    }

    void RequestReresolution() override {}

    absl::string_view GetTarget() override { return fuzzer_->target_; }

    absl::string_view GetAuthority() override { return fuzzer_->authority_; }

    RefCountedPtr<grpc_channel_credentials> GetChannelCredentials() override {
      return nullptr;
    }

    RefCountedPtr<grpc_channel_credentials> GetUnsafeChannelCredentials()
        override {
      return nullptr;
    }

    EventEngine* GetEventEngine() override {
      return fuzzer_->event_engine_.get();
    }

    GlobalStatsPluginRegistry::StatsPluginGroup& GetStatsPluginGroup()
        override {
      return fuzzer_->stats_plugin_group_;
    }

    void AddTraceEvent(TraceSeverity, absl::string_view) override {}

    Fuzzer* const fuzzer_;
  };

  // A fake MetadataInterface implementation, for use in PickArgs.
  class FakeMetadata : public LoadBalancingPolicy::MetadataInterface {
   public:
    explicit FakeMetadata(std::map<std::string, std::string> metadata)
        : metadata_(std::move(metadata)) {}

   private:
    std::optional<absl::string_view> Lookup(
        absl::string_view key, std::string* /*buffer*/) const override {
      auto it = metadata_.find(std::string(key));
      if (it == metadata_.end()) return std::nullopt;
      return it->second;
    }

    std::map<std::string, std::string> metadata_;
  };

  // A fake CallState implementation, for use in PickArgs.
  class FakeCallState : public ClientChannelLbCallState {
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

    ServiceConfigCallData::CallAttributeInterface* GetCallAttribute(
        UniqueTypeName) const override {
      return nullptr;
    }

    ClientCallTracer::CallAttemptTracer* GetCallAttemptTracer() const override {
      return nullptr;
    }

    std::vector<void*> allocations_;
  };

  void CreateLbPolicy(
      const pick_first_fuzzer::CreateLbPolicy& create_lb_policy) {
    ChannelArgs channel_args = CreateChannelArgsFromFuzzingConfiguration(
        create_lb_policy.channel_args(), FuzzingEnvironment());
    auto helper = std::make_unique<FakeHelper>(this);
    helper_ = helper.get();
    LoadBalancingPolicy::Args args = {work_serializer_, std::move(helper),
                                      std::move(channel_args)};
    lb_policy_ =
        CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
            "pick_first", std::move(args));
    last_update_num_endpoints_ = 0;
  }

  bool Update(const pick_first_fuzzer::Update& update) {
    if (lb_policy_ == nullptr) return false;
    auto update_args = MakeUpdateArgs(update);
    if (!update_args.has_value()) return false;
    ExecCtx exec_ctx;
    absl::Status status = lb_policy_->UpdateLocked(std::move(*update_args));
    LOG(INFO) << "UpdateLocked() returned status: " << status;
    got_picker_since_last_update_ = false;
    return true;
  }

  static std::optional<RefCountedPtr<LoadBalancingPolicy::Config>> MakeLbConfig(
      absl::string_view config_string) {
    auto json =
        JsonParse(absl::StrCat("[{\"pick_first\":", config_string, "}]"));
    if (!json.ok()) return std::nullopt;
    auto config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            *json);
    if (!config.ok()) return std::nullopt;
    return *config;
  }

  std::optional<LoadBalancingPolicy::UpdateArgs> MakeUpdateArgs(
      const pick_first_fuzzer::Update& update) {
    LoadBalancingPolicy::UpdateArgs update_args;
    // Config.
    std::string config_string;
    switch (update.config_oneof_case()) {
      case pick_first_fuzzer::Update::kConfigString:
        config_string = update.config_string();
        break;
      case pick_first_fuzzer::Update::kConfigJson: {
        auto status = grpc::protobuf::json::MessageToJsonString(
            update.config_json(), &config_string,
            grpc::protobuf::json::JsonPrintOptions());
        if (!status.ok()) return std::nullopt;
        break;
      }
      case pick_first_fuzzer::Update::CONFIG_ONEOF_NOT_SET:
        config_string = "{}";
        break;
    }
    auto config = MakeLbConfig(config_string);
    if (!config.has_value()) return std::nullopt;
    update_args.config = std::move(*config);
    // Addresses.
    if (update.has_endpoint_error()) {
      absl::Status status = ToAbslStatus(update.endpoint_error());
      if (status.ok()) return std::nullopt;
      update_args.addresses = std::move(status);
    } else {
      auto endpoint_addresses_list = MakeEndpointList(update.endpoint_list());
      last_update_num_endpoints_ = endpoint_addresses_list.size();
      update_args.addresses = std::make_shared<EndpointAddressesListIterator>(
          std::move(endpoint_addresses_list));
    }
    // Channel args.
    update_args.args = CreateChannelArgsFromFuzzingConfiguration(
        update.channel_args(), FuzzingEnvironment());
    return update_args;
  }

  static std::optional<grpc_resolved_address> MakeAddress(
      absl::string_view address_uri) {
    auto uri = URI::Parse(address_uri);
    if (!uri.ok()) return std::nullopt;
    grpc_resolved_address address;
    if (!grpc_parse_uri(*uri, &address)) return std::nullopt;
    return address;
  }

  static std::optional<std::string> AddressUriFromProto(
      const pick_first_fuzzer::Address& address_proto) {
    switch (address_proto.type_case()) {
      case pick_first_fuzzer::Address::kUri:
        return address_proto.uri();
      case pick_first_fuzzer::Address::kLocalhostPort:
        return absl::StrCat("ipv4:127.0.0.1:", address_proto.localhost_port());
      case pick_first_fuzzer::Address::TYPE_NOT_SET:
        return std::nullopt;
    }
  }

  static std::vector<grpc_resolved_address> MakeAddressList(
      const pick_first_fuzzer::EndpointList::Endpoint& endpoint) {
    std::vector<grpc_resolved_address> addresses;
    for (const auto& address_proto : endpoint.addresses()) {
      auto address_uri = AddressUriFromProto(address_proto);
      if (!address_uri.has_value()) continue;
      auto address = MakeAddress(*address_uri);
      if (address.has_value()) addresses.push_back(*address);
    }
    return addresses;
  }

  static EndpointAddressesList MakeEndpointList(
      const pick_first_fuzzer::EndpointList& endpoint_list) {
    EndpointAddressesList endpoints;
    for (const auto& endpoint : endpoint_list.endpoints()) {
      auto addresses = MakeAddressList(endpoint);
      if (!addresses.empty()) {
        auto channel_args = CreateChannelArgsFromFuzzingConfiguration(
            endpoint.channel_args(), FuzzingEnvironment());
        endpoints.emplace_back(std::move(addresses), std::move(channel_args));
      }
    }
    return endpoints;
  }

  static absl::Status ToAbslStatus(const pick_first_fuzzer::Status& status) {
    return absl::Status(static_cast<absl::StatusCode>(status.code()),
                        status.message());
  }

  void ExitIdle() {
    if (lb_policy_ == nullptr) return;
    ExecCtx exec_ctx;
    lb_policy_->ExitIdleLocked();
  }

  void ResetBackoff() {
    if (lb_policy_ == nullptr) return;
    ExecCtx exec_ctx;
    lb_policy_->ResetBackoffLocked();
  }

  void SubchannelConnectivityNotification(
      const pick_first_fuzzer::SubchannelConnectivityNotification&
          notification) {
    grpc_connectivity_state new_state =
        static_cast<grpc_connectivity_state>(notification.state());
    if (new_state >= GRPC_CHANNEL_SHUTDOWN) return;
    auto address_uri = AddressUriFromProto(notification.address());
    if (!address_uri.has_value()) return;
    auto address = MakeAddress(*address_uri);
    if (!address.has_value()) return;
    ChannelArgs args = CreateChannelArgsFromFuzzingConfiguration(
        notification.channel_args(), FuzzingEnvironment());
    SubchannelKey key(*address, args);
    auto [it, created] = subchannel_pool_.emplace(
        std::piecewise_construct, std::forward_as_tuple(key),
        std::forward_as_tuple(*address_uri, this));
    auto& subchannel_state = it->second;
    // Set the state only if the subchannel was just created or it's a
    // valid state transition from its current state.
    if (created || IsValidConnectivityStateTransition(
                       subchannel_state.connectivity_state(), new_state)) {
      subchannel_state.SetConnectivityState(
          new_state, ToAbslStatus(notification.status()));
    }
  }

  static bool IsValidConnectivityStateTransition(
      grpc_connectivity_state from_state, grpc_connectivity_state to_state) {
    switch (from_state) {
      case GRPC_CHANNEL_IDLE:
        return to_state == GRPC_CHANNEL_CONNECTING;
      case GRPC_CHANNEL_CONNECTING:
        return to_state == GRPC_CHANNEL_READY ||
               to_state == GRPC_CHANNEL_TRANSIENT_FAILURE;
      case GRPC_CHANNEL_READY:
        return to_state == GRPC_CHANNEL_IDLE;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        return to_state == GRPC_CHANNEL_IDLE;
      default:
        return false;
    }
  }

  void DoPick() {
    if (picker_ == nullptr) return;
    ExecCtx exec_ctx;
    FakeMetadata md({});
    FakeCallState call_state({});
    auto result = picker_->Pick({"/service/method", &md, &call_state});
    Match(
        result.result,
        [&](const LoadBalancingPolicy::PickResult::Complete& complete) {
          CHECK_NE(complete.subchannel.get(), nullptr);
          LOG(INFO) << "Pick returned Complete: "
                    << complete.subchannel->address();
          EXPECT_EQ(state_, GRPC_CHANNEL_READY)
              << ConnectivityStateName(*state_);
        },
        [&](const LoadBalancingPolicy::PickResult::Queue&) {
          LOG(INFO) << "Pick returned Queue";
          EXPECT_THAT(state_, ::testing::AnyOf(GRPC_CHANNEL_IDLE,
                                               GRPC_CHANNEL_CONNECTING))
              << ConnectivityStateName(*state_);
        },
        [&](const LoadBalancingPolicy::PickResult::Fail& fail) {
          LOG(INFO) << "Pick returned Fail: " << fail.status;
          EXPECT_EQ(state_, GRPC_CHANNEL_TRANSIENT_FAILURE)
              << ConnectivityStateName(*state_);
        },
        [&](const LoadBalancingPolicy::PickResult::Drop& drop) {
          LOG(INFO) << "Pick returned Drop: " << drop.status;
          FAIL() << "pick_first picker should never drop";
        });
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  LoadBalancingPolicy::ChannelControlHelper* helper_ = nullptr;
  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
  std::map<SubchannelKey, SubchannelState> subchannel_pool_;
  uint64_t num_subchannels_ = 0;
  uint64_t num_subchannels_connecting_ = 0;
  uint64_t num_subchannels_transient_failure_ = 0;
  uint64_t last_update_num_endpoints_ = 0;
  bool got_picker_since_last_update_ = false;
  GlobalStatsPluginRegistry::StatsPluginGroup stats_plugin_group_;
  std::string target_ = "dns:server.example.com";
  std::string authority_ = "server.example.com";

  // State reported by the LB policy.
  std::optional<grpc_connectivity_state> state_;
  absl::Status status_;
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_;
};

static const char* kBasicCase = R"pb(
  actions { create_lb_policy {} }
  actions {
    update {
      endpoint_list {
        endpoints { addresses { uri: "ipv4:127.0.0.1:1024" } }
        endpoints { addresses { uri: "ipv4:127.0.0.2:1024" } }
        endpoints { addresses { uri: "ipv4:127.0.0.3:1024" } }
      }
    }
  }
  actions { tick { ms: 100 } }
  actions { do_pick {} }
  actions {
    subchannel_connectivity_notification {
      address { uri: "ipv4:127.0.0.1:1024" }
      state: CONNECTING
    }
  }
  actions { tick { ms: 100 } }
  actions { do_pick {} }
  actions {
    subchannel_connectivity_notification {
      address { uri: "ipv4:127.0.0.1:1024" }
      state: READY
    }
  }
  actions { do_pick {} }
)pb";

auto ParseTestProto(const std::string& proto) {
  pick_first_fuzzer::Msg msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg;
}

void Fuzz(const pick_first_fuzzer::Msg& message) {
  Fuzzer fuzzer(message.fuzzing_event_engine_actions());
  for (const auto& action : message.actions()) {
    fuzzer.Act(action);
  }
  fuzzer.CheckCanBecomeReady();
}
FUZZ_TEST(PickFirstFuzzer, Fuzz)
    .WithDomains(::fuzztest::Arbitrary<pick_first_fuzzer::Msg>().WithSeeds(
        {ParseTestProto(kBasicCase)}));

TEST(PickFirstFuzzer, IgnoresOkStatusForEndpointError) {
  Fuzz(ParseTestProto(R"pb(
    actions { update { endpoint_error {} } }
  )pb"));
}

TEST(PickFirstFuzzer, PassesInTfWhenNotYetStartedConnecting) {
  Fuzz(ParseTestProto(R"pb(
    actions { create_lb_policy {} }
    actions { update {} }
    actions {
      update { endpoint_list { endpoints { addresses { localhost_port: 1 } } } }
    }
  )pb"));
}

TEST(PickFirstFuzzer, AllSubchannelsInTransientFailure) {
  Fuzz(ParseTestProto(R"pb(
    actions { create_lb_policy {} }
    actions {
      subchannel_connectivity_notification {
        address { uri: "ipv4:127.0.0.1:1024" }
        state: TRANSIENT_FAILURE
      }
    }
    actions {
      update {
        endpoint_list { endpoints { addresses { uri: "ipv4:127.0.0.1:1024" } } }
      }
    }
    actions { tick { ms: 10 } }
  )pb"));
}

TEST(PickFirstFuzzer, SubchannelGoesBackToIdleButNotificationPending) {
  Fuzz(ParseTestProto(R"pb(
    actions { create_lb_policy {} }
    actions {
      subchannel_connectivity_notification {
        address { uri: "ipv4:127.0.0.1:1024" }
        state: TRANSIENT_FAILURE
      }
    }
    actions {
      update {
        endpoint_list { endpoints { addresses { uri: "ipv4:127.0.0.1:1024" } } }
      }
    }
    actions { tick { ms: 10 } }
    actions {
      subchannel_connectivity_notification {
        address { uri: "ipv4:127.0.0.1:1024" }
        state: IDLE
      }
    }
  )pb"));
}

TEST(PickFirstFuzzer,
     PendingTransientFailureStateNotificationWhenSubchannelUnreffed) {
  Fuzz(ParseTestProto(R"pb(
    actions { create_lb_policy {} }
    actions {
      update {
        endpoint_list { endpoints { addresses { uri: "ipv4:127.0.0.1:1024" } } }
      }
    }
    actions {
      subchannel_connectivity_notification {
        address { uri: "ipv4:127.0.0.1:1024" }
        state: CONNECTING
      }
    }
    actions { tick { ms: 100 } }
    actions {
      subchannel_connectivity_notification {
        address { uri: "ipv4:127.0.0.1:1024" }
        state: TRANSIENT_FAILURE
      }
    }
    actions {
      update {
        endpoint_list { endpoints { addresses { uri: "ipv4:127.0.0.2:1024" } } }
      }
    }
  )pb"));
}

TEST(PickFirstFuzzer, TwoSubchannelsWithSameAddress) {
  Fuzz(ParseTestProto(R"pb(
    actions { create_lb_policy {} }
    actions {
      update {
        endpoint_list {
          endpoints { addresses { localhost_port: 1024 } }
          endpoints { addresses { localhost_port: 1024 } }
        }
      }
    }
    actions { tick { ms: 1 } }
    actions {
      subchannel_connectivity_notification {
        address { localhost_port: 1024 }
        state: CONNECTING
      }
    }
    actions {
      subchannel_connectivity_notification {
        address { localhost_port: 1024 }
        state: TRANSIENT_FAILURE
      }
    }
    actions { tick { ms: 1 } }
  )pb"));
}

}  // namespace testing
}  // namespace grpc_core
