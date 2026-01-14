// Copyright 2024 gRPC authors.
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

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>

#include <memory>

#include "src/core/client_channel/subchannel_interface_internal.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/health_check_client_internal.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/util/json/json_reader.h"
#include "test/core/test_util/build.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

bool IsSlowBuild() {
  return BuiltUnderMsan() || BuiltUnderUbsan() || BuiltUnderTsan();
}

class BenchmarkHelper : public std::enable_shared_from_this<BenchmarkHelper> {
 public:
  BenchmarkHelper(absl::string_view name, absl::string_view config)
      : name_(name), config_json_(config) {
    CHECK(lb_policy_ != nullptr) << "Failed to create LB policy: " << name;
    auto parsed_json = JsonParse(std::string(config_json_));
    CHECK_OK(parsed_json);
    auto config_parsed =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            *parsed_json);
    CHECK_OK(config_parsed);
    config_ = std::move(*config_parsed);
  }

  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> GetPicker() {
    MutexLock lock(&mu_);
    while (picker_ == nullptr) {
      cv_.Wait(&mu_);
    }
    return picker_;
  }

  void UpdateLbPolicy(size_t num_endpoints) {
    {
      MutexLock lock(&mu_);
      picker_ = nullptr;
      work_serializer_->Run([this, num_endpoints]() {
        EndpointAddressesList addresses;
        for (size_t i = 0; i < num_endpoints; i++) {
          int port = i % 65536;
          int ip = i / 65536;
          std::string addr = absl::StrCat("ipv4:127.0.0.", ip, ":", port);
          addresses.emplace_back(addr, ChannelArgs());
        }
        CHECK_OK(lb_policy_->UpdateLocked(LoadBalancingPolicy::UpdateArgs{
            std::make_shared<EndpointAddressesListIterator>(
                std::move(addresses)),
            config_, "", ChannelArgs()}));
      });
    }
  }

 private:
  class SubchannelFake final : public SubchannelInterface {
   public:
    explicit SubchannelFake(BenchmarkHelper* helper) : helper_(helper) {}

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface> unique_watcher)
        override {
      AddConnectivityWatcherInternal(
          std::shared_ptr<ConnectivityStateWatcherInterface>(
              std::move(unique_watcher)));
    }

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override {
      MutexLock lock(&helper_->mu_);
      helper_->connectivity_watchers_.erase(watcher);
    }

    void RequestConnection() override { LOG(FATAL) << "unimplemented"; }

    void ResetBackoff() override { LOG(FATAL) << "unimplemented"; }

    void AddDataWatcher(
        std::unique_ptr<DataWatcherInterface> watcher) override {
      auto* watcher_internal =
          DownCast<InternalSubchannelDataWatcherInterface*>(watcher.get());
      if (watcher_internal->type() == HealthProducer::Type()) {
        AddConnectivityWatcherInternal(
            DownCast<HealthWatcher*>(watcher_internal)->TakeWatcher());
      } else {
        LOG(FATAL) << "unimplemented watcher type: "
                   << watcher_internal->type();
      }
    }

    void CancelDataWatcher(DataWatcherInterface* watcher) override {}

    std::string address() const override { return "test"; }

   private:
    void AddConnectivityWatcherInternal(
        std::shared_ptr<ConnectivityStateWatcherInterface> watcher) {
      {
        MutexLock lock(&helper_->mu_);
        helper_->work_serializer_->Run([watcher]() {
          watcher->OnConnectivityStateChange(GRPC_CHANNEL_READY,
                                             absl::OkStatus());
        });
        helper_->connectivity_watchers_.insert(std::move(watcher));
      }
    }

    BenchmarkHelper* helper_;
  };

  class LbHelper final : public LoadBalancingPolicy::ChannelControlHelper {
   public:
    explicit LbHelper(BenchmarkHelper* helper) : helper_(helper) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const std::string& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args) override {
      return MakeRefCounted<SubchannelFake>(helper_);
    }

    void UpdateState(
        grpc_connectivity_state state, const absl::Status& status,
        RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
      MutexLock lock(&helper_->mu_);
      helper_->picker_ = std::move(picker);
      helper_->cv_.SignalAll();
    }

    void RequestReresolution() override { LOG(FATAL) << "unimplemented"; }

    absl::string_view GetTarget() override { return "foo"; }

    absl::string_view GetAuthority() override { return "foo"; }

    RefCountedPtr<grpc_channel_credentials> GetChannelCredentials() override {
      LOG(FATAL) << "unimplemented";
    }

    RefCountedPtr<grpc_channel_credentials> GetUnsafeChannelCredentials()
        override {
      LOG(FATAL) << "unimplemented";
    }

    grpc_event_engine::experimental::EventEngine* GetEventEngine() override {
      return helper_->event_engine_.get();
    }

    GlobalStatsPluginRegistry::StatsPluginGroup& GetStatsPluginGroup()
        override {
      return *helper_->stats_plugin_group_;
    }

    void AddTraceEvent(absl::string_view message) override {
      LOG(FATAL) << "unimplemented";
    }

    BenchmarkHelper* helper_;
  };

  const absl::string_view name_;
  const absl::string_view config_json_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  std::shared_ptr<WorkSerializer> work_serializer_ =
      std::make_shared<WorkSerializer>(event_engine_);
  OrphanablePtr<LoadBalancingPolicy> lb_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          name_, LoadBalancingPolicy::Args{work_serializer_,
                                           std::make_unique<LbHelper>(this),
                                           ChannelArgs()});
  RefCountedPtr<LoadBalancingPolicy::Config> config_;
  Mutex mu_;
  CondVar cv_;
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_
      ABSL_GUARDED_BY(mu_);
  absl::flat_hash_set<
      std::shared_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>>
      connectivity_watchers_ ABSL_GUARDED_BY(mu_);
  std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
      stats_plugin_group_ =
          GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
              experimental::StatsPluginChannelScope(
                  "foo", "foo",
                  grpc_event_engine::experimental::ChannelArgsEndpointConfig{
                      ChannelArgs{}}));
};

void BM_Pick(benchmark::State& state, BenchmarkHelper& helper) {
  helper.UpdateLbPolicy(state.range(0));
  auto picker = helper.GetPicker();
  for (auto _ : state) {
    picker->Pick(LoadBalancingPolicy::PickArgs{
        "/foo/bar",
        nullptr,
        nullptr,
    });
  }
}
#define PICKER_BENCHMARK(policy, config)                        \
  BENCHMARK_CAPTURE(BM_Pick, policy,                            \
                    []() -> BenchmarkHelper& {                  \
                      static auto* helper =                     \
                          new BenchmarkHelper(#policy, config); \
                      return *helper;                           \
                    }())                                        \
      ->RangeMultiplier(10)                                     \
      ->Range(1, IsSlowBuild() ? 1000 : 100000)

PICKER_BENCHMARK(pick_first, "[{\"pick_first\":{}}]");
PICKER_BENCHMARK(
    weighted_round_robin,
    "[{\"weighted_round_robin\":{\"enableOobLoadReport\":false}}]");

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  grpc_init();
  benchmark::RunTheBenchmarksNamespaced();
  grpc_shutdown();
  return 0;
}
