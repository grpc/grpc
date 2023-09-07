//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/util/test_config.h"

class ResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  void SetExpectedAndEvent(grpc_core::Resolver::Result expected,
                           gpr_event* ev) {
    ASSERT_EQ(ev_, nullptr);
    expected_ = std::move(expected);
    ev_ = ev;
  }

  void ReportResult(grpc_core::Resolver::Result actual) override {
    ASSERT_NE(ev_, nullptr);
    // We only check the addresses, because that's the only thing
    // explicitly set by the test via
    // FakeResolverResponseGenerator::SetResponse().
    ASSERT_TRUE(actual.addresses.ok());
    ASSERT_EQ(actual.addresses->size(), expected_.addresses->size());
    for (size_t i = 0; i < expected_.addresses->size(); ++i) {
      ASSERT_EQ((*actual.addresses)[i], (*expected_.addresses)[i]);
    }
    gpr_event_set(ev_, reinterpret_cast<void*>(1));
    ev_ = nullptr;
  }

 private:
  grpc_core::Resolver::Result expected_;
  gpr_event* ev_ = nullptr;
};

static grpc_core::OrphanablePtr<grpc_core::Resolver> build_fake_resolver(
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer,
    grpc_core::FakeResolverResponseGenerator* response_generator,
    std::unique_ptr<grpc_core::Resolver::ResultHandler> result_handler) {
  grpc_core::ResolverFactory* factory = grpc_core::CoreConfiguration::Get()
                                            .resolver_registry()
                                            .LookupResolverFactory("fake");
  grpc_arg generator_arg =
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator);
  grpc_channel_args channel_args = {1, &generator_arg};
  grpc_core::ResolverArgs args;
  args.args = grpc_core::ChannelArgs::FromC(&channel_args);
  args.work_serializer = std::move(work_serializer);
  args.result_handler = std::move(result_handler);
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  return resolver;
}

// Create a new resolution containing 2 addresses.
static grpc_core::Resolver::Result create_new_resolver_result() {
  static size_t test_counter = 0;
  const size_t num_addresses = 2;
  // Create address list.
  grpc_core::ServerAddressList addresses;
  for (size_t i = 0; i < num_addresses; ++i) {
    std::string uri_string = absl::StrFormat("ipv4:127.0.0.1:100%" PRIuPTR,
                                             test_counter * num_addresses + i);
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_string);
    EXPECT_TRUE(uri.ok());
    grpc_resolved_address address;
    EXPECT_TRUE(grpc_parse_uri(*uri, &address));
    absl::InlinedVector<grpc_arg, 2> args_to_add;
    addresses.emplace_back(address, grpc_core::ChannelArgs());
  }
  ++test_counter;
  grpc_core::Resolver::Result result;
  result.addresses = std::move(addresses);
  return result;
}

TEST(FakeResolverTest, FakeResolver) {
  grpc_core::ExecCtx exec_ctx;
  std::shared_ptr<grpc_core::WorkSerializer> work_serializer =
      std::make_shared<grpc_core::WorkSerializer>(
          grpc_event_engine::experimental::GetDefaultEventEngine());
  // Create resolver.
  ResultHandler* result_handler = new ResultHandler();
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator =
          grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver = build_fake_resolver(
      work_serializer, response_generator.get(),
      std::unique_ptr<grpc_core::Resolver::ResultHandler>(result_handler));
  ASSERT_NE(resolver.get(), nullptr);
  resolver->StartLocked();
  // Test 1: normal resolution.
  // next_results != NULL, reresolution_results == NULL.
  // Expected response is next_results.
  gpr_log(GPR_INFO, "TEST 1");
  grpc_core::Resolver::Result result = create_new_resolver_result();
  gpr_event ev1;
  gpr_event_init(&ev1);
  result_handler->SetExpectedAndEvent(result, &ev1);
  response_generator->SetResponseSynchronously(std::move(result));
  grpc_core::ExecCtx::Get()->Flush();
  ASSERT_NE(gpr_event_wait(&ev1, grpc_timeout_seconds_to_deadline(5)), nullptr);
  // Test 2: update resolution.
  // next_results != NULL, reresolution_results == NULL.
  // Expected response is next_results.
  gpr_log(GPR_INFO, "TEST 2");
  result = create_new_resolver_result();
  gpr_event ev2;
  gpr_event_init(&ev2);
  result_handler->SetExpectedAndEvent(result, &ev2);
  response_generator->SetResponseSynchronously(std::move(result));
  grpc_core::ExecCtx::Get()->Flush();
  ASSERT_NE(gpr_event_wait(&ev2, grpc_timeout_seconds_to_deadline(5)), nullptr);
  // Test 3: normal re-resolution.
  // next_results == NULL, reresolution_results != NULL.
  // Expected response is reresolution_results.
  gpr_log(GPR_INFO, "TEST 3");
  grpc_core::Resolver::Result reresolution_result =
      create_new_resolver_result();
  gpr_event ev3;
  gpr_event_init(&ev3);
  result_handler->SetExpectedAndEvent(reresolution_result, &ev3);
  // Set reresolution_results.
  // No result will be returned until re-resolution is requested.
  response_generator->SetReresolutionResponseSynchronously(reresolution_result);
  grpc_core::ExecCtx::Get()->Flush();
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  ASSERT_NE(gpr_event_wait(&ev3, grpc_timeout_seconds_to_deadline(5)), nullptr);
  // Test 4: repeat re-resolution.
  // next_results == NULL, reresolution_results != NULL.
  // Expected response is reresolution_results.
  gpr_log(GPR_INFO, "TEST 4");
  gpr_event ev4;
  gpr_event_init(&ev4);
  result_handler->SetExpectedAndEvent(std::move(reresolution_result), &ev4);
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  ASSERT_NE(gpr_event_wait(&ev4, grpc_timeout_seconds_to_deadline(5)), nullptr);
  // Test 5: normal resolution.
  // next_results != NULL, reresolution_results != NULL.
  // Expected response is next_results.
  gpr_log(GPR_INFO, "TEST 5");
  result = create_new_resolver_result();
  gpr_event ev5;
  gpr_event_init(&ev5);
  result_handler->SetExpectedAndEvent(result, &ev5);
  response_generator->SetResponseSynchronously(std::move(result));
  grpc_core::ExecCtx::Get()->Flush();
  ASSERT_NE(gpr_event_wait(&ev5, grpc_timeout_seconds_to_deadline(5)), nullptr);
  // Test 6: no-op.
  // Requesting a new resolution without setting the response shouldn't trigger
  // the resolution callback.
  gpr_log(GPR_INFO, "TEST 6");
  gpr_event ev6;
  gpr_event_init(&ev6);
  result_handler->SetExpectedAndEvent(grpc_core::Resolver::Result(), &ev6);
  ASSERT_EQ(gpr_event_wait(&ev6, grpc_timeout_milliseconds_to_deadline(100)),
            nullptr);
  // Clean up.
  resolver.reset();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
