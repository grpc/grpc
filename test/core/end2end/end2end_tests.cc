// Copyright 2015 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <google/protobuf/text_format.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>

#include <memory>
#include <optional>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;
using grpc_event_engine::experimental::SetDefaultEventEngine;

namespace grpc_core {

Slice RandomSlice(size_t length) {
  size_t i;
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  std::vector<char> output;
  output.resize(length);
  for (i = 0; i < length; ++i) {
    output[i] = chars[rand() % static_cast<int>(sizeof(chars) - 1)];
  }
  return Slice::FromCopiedBuffer(output);
}

Slice RandomBinarySlice(size_t length) {
  size_t i;
  std::vector<uint8_t> output;
  output.resize(length);
  for (i = 0; i < length; ++i) {
    output[i] = rand();
  }
  return Slice::FromCopiedBuffer(output);
}

CoreEnd2endTest::CoreEnd2endTest(
    const CoreTestConfiguration* config,
    const core_end2end_test_fuzzer::Msg* fuzzing_args,
    absl::string_view suite_name)
    : test_config_(config), fuzzing_(fuzzing_args != nullptr) {
  if (fuzzing_args != nullptr) {
    ConfigVars::Overrides overrides =
        OverridesFromFuzzConfigVars(fuzzing_args->config_vars());
    overrides.default_ssl_roots_file_path = CA_CERT_PATH;
    if (suite_name == "NoLoggingTests") overrides.trace = std::nullopt;
    ConfigVars::SetOverrides(overrides);
    TestOnlyReloadExperimentsFromConfigVariables();
    FuzzingEventEngine::Options options;
    options.max_delay_run_after = std::chrono::milliseconds(500);
    options.max_delay_write = std::chrono::microseconds(5);
    auto engine = std::make_shared<FuzzingEventEngine>(
        options, fuzzing_args->event_engine_actions());
    SetDefaultEventEngine(std::static_pointer_cast<EventEngine>(engine));
    SetQuiesceEventEngine(
        [](std::shared_ptr<grpc_event_engine::experimental::EventEngine>&& ee) {
          static_cast<FuzzingEventEngine*>(ee.get())->TickUntilIdle();
          ee.reset();
        });
    SetCqVerifierStepFn(
        [engine = std::move(engine)](
            grpc_event_engine::experimental::EventEngine::Duration max_step) {
          ExecCtx exec_ctx;
          engine->Tick(max_step);
          grpc_timer_manager_tick();
        });
    SetPostGrpcInitFunc([]() { grpc_timer_manager_set_threading(false); });
  } else {
    ConfigVars::Overrides overrides;
    overrides.default_ssl_roots_file_path = CA_CERT_PATH;
    ConfigVars::SetOverrides(overrides);
  }
  CoreConfiguration::Reset();
  initialized_ = false;
  grpc_prewarm_os_for_tests();
}

CoreEnd2endTest::~CoreEnd2endTest() {
  const bool do_shutdown = fixture_ != nullptr;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee;
  if (grpc_is_initialized()) {
    ee = grpc_event_engine::experimental::GetDefaultEventEngine();
  }
  ShutdownAndDestroyClient();
  ShutdownAndDestroyServer();
  cq_verifier_.reset();
  if (cq_ != nullptr) {
    grpc_completion_queue_shutdown(cq_);
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
    cq_ = nullptr;
  }
  fixture_.reset();
  // Creating an EventEngine requires gRPC initialization, which the NoOp test
  // does not do. Skip the EventEngine check if unnecessary.
  if (ee != nullptr) {
    quiesce_event_engine_(std::move(ee));
  }
  if (do_shutdown) {
    grpc_shutdown_blocking();
    // This will wait until gRPC shutdown has actually happened to make sure
    // no gRPC resources (such as thread) are active. (timeout = 10s)
    if (!grpc_wait_until_shutdown(10)) {
      LOG(ERROR) << "Timeout in waiting for gRPC shutdown";
    }
  }
  CHECK_EQ(client_, nullptr);
  CHECK_EQ(server_, nullptr);
  initialized_ = false;
}

CoreEnd2endTest::Call CoreEnd2endTest::ClientCallBuilder::Create() {
  if (auto* u = std::get_if<UnregisteredCall>(&call_selector_)) {
    std::optional<Slice> host;
    if (u->host.has_value()) host = Slice::FromCopiedString(*u->host);
    test_.ForceInitialized();
    return Call(
        grpc_channel_create_call(
            test_.client(), parent_call_, propagation_mask_, test_.cq(),
            Slice::FromCopiedString(u->method).c_slice(),
            host.has_value() ? &host->c_slice() : nullptr, deadline_, nullptr),
        &test_);
  } else {
    return Call(grpc_channel_create_registered_call(
                    test_.client(), parent_call_, propagation_mask_, test_.cq(),
                    std::get<void*>(call_selector_), deadline_, nullptr),
                &test_);
  }
}

CoreEnd2endTest::ServerRegisteredMethod::ServerRegisteredMethod(
    CoreEnd2endTest* test, absl::string_view name,
    grpc_server_register_method_payload_handling payload_handling) {
  CHECK_EQ(test->server_, nullptr);
  test->pre_server_start_ = [old = std::move(test->pre_server_start_),
                             handle = handle_, name = std::string(name),
                             payload_handling](grpc_server* server) mutable {
    *handle = grpc_server_register_method(server, name.c_str(), nullptr,
                                          payload_handling, 0);
    old(server);
  };
}

CoreEnd2endTest::IncomingCall::IncomingCall(CoreEnd2endTest& test, int tag)
    : impl_(std::make_unique<Impl>(&test)) {
  test.ForceInitialized();
  EXPECT_EQ(
      grpc_server_request_call(test.server(), impl_->call.call_ptr(),
                               &impl_->call_details, &impl_->request_metadata,
                               test.cq(), test.cq(), CqVerifier::tag(tag)),
      GRPC_CALL_OK);
}

CoreEnd2endTest::IncomingCall::IncomingCall(CoreEnd2endTest& test, void* method,
                                            IncomingMessage* message, int tag)
    : impl_(std::make_unique<Impl>(&test)) {
  test.ForceInitialized();
  impl_->call_details.method = grpc_empty_slice();
  EXPECT_EQ(grpc_server_request_registered_call(
                test.server(), method, impl_->call.call_ptr(),
                &impl_->call_details.deadline, &impl_->request_metadata,
                message == nullptr ? nullptr : message->raw_payload_ptr(),
                test.cq(), test.cq(), CqVerifier::tag(tag)),
            GRPC_CALL_OK);
}

std::optional<std::string> CoreEnd2endTest::IncomingCall::GetInitialMetadata(
    absl::string_view key) const {
  return FindInMetadataArray(impl_->request_metadata, key);
}

void CoreEnd2endTest::ForceInitialized() {
  if (!initialized_) {
    initialized_ = true;
    InitServer(DefaultServerArgs());
    InitClient(ChannelArgs());
  }
}

core_end2end_test_fuzzer::Msg ParseTestProto(std::string text) {
  core_end2end_test_fuzzer::Msg msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(text, &msg));
  return msg;
}

}  // namespace grpc_core
