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

#include "test/core/end2end/fuzzers/connector_fuzzer.h"

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "test/core/end2end/fuzzers/network_input.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/test_config.h"

bool squelch = true;
bool leak_check = true;

using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::SetEventEngineFactory;

namespace grpc_core {
namespace {

class ConnectorFuzzer {
 public:
  ConnectorFuzzer(
      const fuzzer_input::Msg& msg,
      absl::FunctionRef<OrphanablePtr<SubchannelConnector>()> make_connector)
      : engine_([actions = msg.event_engine_actions()]() {
          SetEventEngineFactory(
              [actions]() -> std::unique_ptr<
                              grpc_event_engine::experimental::EventEngine> {
                return std::make_unique<FuzzingEventEngine>(
                    FuzzingEventEngine::Options(), actions);
              });
          return std::dynamic_pointer_cast<FuzzingEventEngine>(
              GetDefaultEventEngine());
        }()),
        mock_endpoint_controller_(
            grpc_event_engine::experimental::MockEndpointController::Create(
                engine_)),
        connector_(make_connector()) {
    CHECK(engine_);
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    ExecCtx exec_ctx;
    Executor::SetThreadingAll(false);
    ScheduleReads(msg.network_input()[0], mock_endpoint_controller_,
                  engine_.get());
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
  }

  ~ConnectorFuzzer() {
    connector_.reset();
    mock_endpoint_controller_.reset();
    engine_->TickUntilIdle();
    grpc_shutdown_blocking();
    engine_->UnsetGlobalHooks();
  }

  void Run() {
    OrphanablePtr<grpc_endpoint> endpoint(
        mock_endpoint_controller_->TakeCEndpoint());
    SubchannelConnector::Result result;
    bool done = false;
    grpc_resolved_address addr;
    CHECK(grpc_parse_uri(URI::Parse("ipv4:127.0.0.1:1234").value(), &addr));
    connector_->Connect(
        SubchannelConnector::Args{
            &addr, nullptr, Timestamp::Now() + Duration::Seconds(20),
            ChannelArgs{}
                .SetObject<grpc_event_engine::experimental::EventEngine>(
                    engine_)},
        &result, NewClosure([&done](grpc_error_handle) { done = true; }));

    while (!done) {
      engine_->Tick();
      grpc_timer_manager_tick();
    }
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine> engine_;
  std::shared_ptr<grpc_event_engine::experimental::MockEndpointController>
      mock_endpoint_controller_;
  OrphanablePtr<SubchannelConnector> connector_;
};

}  // namespace

void RunConnectorFuzzer(
    const fuzzer_input::Msg& msg,
    absl::FunctionRef<OrphanablePtr<SubchannelConnector>()> make_connector) {
  if (squelch && !GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    grpc_disable_all_absl_logs();
  }
  static const int once = []() {
    ForceEnableExperiment("event_engine_client", true);
    ForceEnableExperiment("event_engine_listener", true);
    return 42;
  }();
  CHECK_EQ(once, 42);  // avoid unused variable warning
  ApplyFuzzConfigVars(msg.config_vars());
  TestOnlyReloadExperimentsFromConfigVariables();
  ConnectorFuzzer(msg, make_connector).Run();
}

}  // namespace grpc_core
