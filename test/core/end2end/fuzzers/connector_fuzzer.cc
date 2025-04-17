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

#include <google/protobuf/text_format.h>

#include <memory>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/credentials/transport/fake/fake_security_connector.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/util/env.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/end2end/fuzzers/network_input.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/test_config.h"

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::MockEndpointController;
using ::grpc_event_engine::experimental::URIToResolvedAddress;

namespace grpc_core {
namespace {

class ConnectorFuzzer {
 public:
  ConnectorFuzzer(
      const fuzzer_input::Msg& msg,
      absl::FunctionRef<RefCountedPtr<grpc_channel_security_connector>()>
          make_security_connector,
      absl::FunctionRef<OrphanablePtr<SubchannelConnector>()> make_connector)
      : make_security_connector_(make_security_connector),
        engine_(std::make_shared<FuzzingEventEngine>(
            FuzzingEventEngine::Options(), msg.event_engine_actions())),
        mock_endpoint_controller_(MockEndpointController::Create(engine_)),
        connector_(make_connector()) {
    CHECK(engine_);
    grpc_event_engine::experimental::SetDefaultEventEngine(engine_);
    for (const auto& input : msg.network_input()) {
      network_inputs_.push(input);
    }
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    ExecCtx exec_ctx;
    Executor::SetThreadingAll(false);
    listener_ =
        engine_
            ->CreateListener(
                [this](std::unique_ptr<EventEngine::Endpoint> endpoint,
                       MemoryAllocator) {
                  if (network_inputs_.empty()) return;
                  ScheduleWrites(network_inputs_.front(), std::move(endpoint),
                                 engine_.get());
                  network_inputs_.pop();
                },
                [](absl::Status) {}, ChannelArgsEndpointConfig(ChannelArgs{}),
                std::make_unique<MemoryQuota>("foo"))
            .value();
    if (msg.has_shutdown_connector() &&
        msg.shutdown_connector().delay_ms() > 0) {
      auto shutdown_connector = msg.shutdown_connector();
      const auto delay = Duration::Milliseconds(shutdown_connector.delay_ms());
      engine_->RunAfterExactly(delay, [this, shutdown_connector = std::move(
                                                 shutdown_connector)]() {
        if (connector_ == nullptr) return;
        connector_->Shutdown(absl::Status(
            static_cast<absl::StatusCode>(shutdown_connector.shutdown_status()),
            shutdown_connector.shutdown_message()));
      });
    }
    // Abbreviated runtime for interpreting API actions, since we simply don't
    // support many here.
    uint64_t when_ms = 0;
    for (const auto& action : msg.api_actions()) {
      switch (action.type_case()) {
        default:
          break;
        case api_fuzzer::Action::kSleepMs:
          when_ms += action.sleep_ms();
          break;
        case api_fuzzer::Action::kResizeResourceQuota:
          engine_->RunAfterExactly(
              Duration::Milliseconds(when_ms),
              [this, new_size = action.resize_resource_quota()]() {
                ExecCtx exec_ctx;
                resource_quota_->memory_quota()->SetSize(new_size);
              });
          when_ms += 1;
          break;
      }
    }
  }

  ~ConnectorFuzzer() {
    listener_.reset();
    connector_.reset();
    mock_endpoint_controller_.reset();
    engine_->TickUntilIdle();
    grpc_shutdown_blocking();
    engine_->UnsetGlobalHooks();
    // The engine ref must be released for ShutdownDefaultEventEngine to finish.
    engine_.reset();
    grpc_event_engine::experimental::ShutdownDefaultEventEngine();
  }

  void Run() {
    grpc_resolved_address addr;
    CHECK(grpc_parse_uri(URI::Parse("ipv4:127.0.0.1:1234").value(), &addr));
    CHECK_OK(
        listener_->Bind(URIToResolvedAddress("ipv4:127.0.0.1:1234").value()));
    CHECK_OK(listener_->Start());
    OrphanablePtr<grpc_endpoint> endpoint(
        mock_endpoint_controller_->TakeCEndpoint());
    SubchannelConnector::Result result;
    bool done = false;
    auto channel_args = ChannelArgs{}.SetObject<EventEngine>(engine_).SetObject(
        resource_quota_);
    auto security_connector = make_security_connector_();
    if (security_connector != nullptr) {
      channel_args = channel_args.SetObject(std::move(security_connector));
    }
    connector_->Connect(
        SubchannelConnector::Args{&addr, nullptr,
                                  Timestamp::Now() + Duration::Seconds(20),
                                  channel_args},
        &result, NewClosure([&done, &result](grpc_error_handle status) {
          done = true;
          if (status.ok()) result.transport->Orphan();
        }));

    while (!done) {
      engine_->Tick();
      grpc_timer_manager_tick();
    }
  }

 private:
  RefCountedPtr<ResourceQuota> resource_quota_ =
      MakeRefCounted<ResourceQuota>("fuzzer");
  absl::FunctionRef<RefCountedPtr<grpc_channel_security_connector>()>
      make_security_connector_;
  std::shared_ptr<FuzzingEventEngine> engine_;
  std::queue<fuzzer_input::NetworkInput> network_inputs_;
  std::shared_ptr<MockEndpointController> mock_endpoint_controller_;
  std::unique_ptr<EventEngine::Listener> listener_;
  OrphanablePtr<SubchannelConnector> connector_;
};

void RunConnectorFuzzer(
    const fuzzer_input::Msg& msg,
    absl::FunctionRef<RefCountedPtr<grpc_channel_security_connector>()>
        make_security_connector,
    absl::FunctionRef<OrphanablePtr<SubchannelConnector>()> make_connector) {
  if (!IsEventEngineClientEnabled() || !IsEventEngineListenerEnabled()) {
    return;  // Not supported without event engine
  }
  ApplyFuzzConfigVars(msg.config_vars());
  TestOnlyReloadExperimentsFromConfigVariables();
  ConnectorFuzzer(msg, make_security_connector, make_connector).Run();
}

auto ParseTestProto(const std::string& proto) {
  fuzzer_input::Msg msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg;
}

void Chttp2(fuzzer_input::Msg msg) {
  RunConnectorFuzzer(
      msg, []() { return RefCountedPtr<grpc_channel_security_connector>(); },
      []() { return MakeOrphanable<Chttp2Connector>(); });
}
FUZZ_TEST(ConnectorFuzzers, Chttp2);

void Chttp2Fakesec(fuzzer_input::Msg msg) {
  RunConnectorFuzzer(
      msg,
      []() {
        return grpc_fake_channel_security_connector_create(
            RefCountedPtr<grpc_channel_credentials>(
                grpc_fake_transport_security_credentials_create()),
            nullptr, "foobar", ChannelArgs{});
      },
      []() { return MakeOrphanable<Chttp2Connector>(); });
}
FUZZ_TEST(ConnectorFuzzers, Chttp2Fakesec);

TEST(ConnectorFuzzers, Chttp2FakesecTimeout1) {
  Chttp2Fakesec(ParseTestProto(R"pb(network_input {
                                      input_segments {
                                        segments { delay_ms: 1 }
                                        segments {
                                          delay_ms: 1
                                          chaotic_good {
                                            known_type: SETTINGS
                                            payload_empty_of_length: 2147483647
                                          }
                                        }
                                      }
                                      connect_delay_ms: -1603816748
                                      connect_timeout_ms: 3
                                    }
                                    event_engine_actions {
                                      run_delay: 1
                                      assign_ports: 1
                                      assign_ports: 2147483647
                                      connections {}
                                    }
                                    config_vars { verbosity: "" })pb"));
}

}  // namespace
}  // namespace grpc_core
