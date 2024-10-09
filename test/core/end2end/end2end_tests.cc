
//
//
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
//
//

#include "test/core/end2end/end2end_tests.h"

#include <regex>
#include <tuple>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"

#include <grpc/byte_buffer_reader.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/no_destruct.h"
#include "test/core/end2end/cq_verifier.h"

namespace grpc_core {

bool g_is_fuzzing_core_e2e_tests = false;

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

void CoreEnd2endTest::SetUp() {
  CoreConfiguration::Reset();
  initialized_ = false;
}

void CoreEnd2endTest::TearDown() {
  const bool do_shutdown = fixture_ != nullptr;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee;
// TODO(hork): locate the windows leak so we can enable end2end experiments.
#ifndef GPR_WINDOWS
  if (grpc_is_initialized()) {
    ee = grpc_event_engine::experimental::GetDefaultEventEngine();
  }
#endif
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
  if (auto* u = absl::get_if<UnregisteredCall>(&call_selector_)) {
    absl::optional<Slice> host;
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
                    absl::get<void*>(call_selector_), deadline_, nullptr),
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

absl::optional<std::string> CoreEnd2endTest::IncomingCall::GetInitialMetadata(
    absl::string_view key) const {
  return FindInMetadataArray(impl_->request_metadata, key);
}

void CoreEnd2endTest::ForceInitialized() {
  if (!initialized_) {
    initialized_ = true;
    InitServer(ChannelArgs());
    InitClient(ChannelArgs());
  }
}

void CoreEnd2endTestRegistry::RegisterTest(absl::string_view suite,
                                           absl::string_view name,
                                           MakeTestFn make_test,
                                           SourceLocation) {
  if (absl::StartsWith(name, "DISABLED_")) return;
  auto& tests = tests_by_suite_[suite];
  CHECK_EQ(tests.count(name), 0u);
  tests[name] = std::move(make_test);
}

void CoreEnd2endTestRegistry::RegisterSuite(
    absl::string_view suite, std::vector<const CoreTestConfiguration*> configs,
    SourceLocation) {
  CHECK_EQ(suites_.count(suite), 0u);
  suites_[suite] = std::move(configs);
}

namespace {
template <typename Map>
std::vector<absl::string_view> KeysFrom(const Map& map) {
  std::vector<absl::string_view> out;
  out.reserve(map.size());
  for (const auto& elem : map) {
    out.push_back(elem.first);
  }
  return out;
}
}  // namespace

std::vector<CoreEnd2endTestRegistry::Test> CoreEnd2endTestRegistry::AllTests() {
  std::vector<Test> tests;
  // Sort inputs to ensure outputs are deterministic
  for (auto& suite_configs : suites_) {
    std::sort(suite_configs.second.begin(), suite_configs.second.end(),
              [](const auto* a, const auto* b) { return a->name < b->name; });
  }
  for (const auto& suite_configs : suites_) {
    if (suite_configs.second.empty()) {
      fprintf(
          stderr, "%s\n",
          absl::StrCat("Suite ", suite_configs.first, " has no tests").c_str());
    }
    for (const auto& test_factory : tests_by_suite_[suite_configs.first]) {
      for (const auto* config : suite_configs.second) {
        tests.push_back(Test{suite_configs.first, test_factory.first, config,
                             test_factory.second});
      }
    }
  }
  return tests;
}

}  // namespace grpc_core
