/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"

#include "test/core/util/test_config.h"

class ResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  ~ResultHandler() override { grpc_channel_args_destroy(expected_); }

  void SetExpectedAndEvent(grpc_channel_args* expected, gpr_event* ev) {
    GPR_ASSERT(expected_ == nullptr);
    GPR_ASSERT(ev_ == nullptr);
    expected_ = grpc_channel_args_copy(expected);
    ev_ = ev;
  }

  void ReturnResult(const grpc_channel_args* args) override {
    GPR_ASSERT(expected_ != nullptr);
    GPR_ASSERT(ev_ != nullptr);
    // We only check the addresses channel arg because that's the only one
    // explicitly set by the test via
    // FakeResolverResponseGenerator::SetResponse().
    const grpc_core::ServerAddressList* actual_addresses =
        grpc_core::FindServerAddressListChannelArg(args);
    const grpc_core::ServerAddressList* expected_addresses =
        grpc_core::FindServerAddressListChannelArg(expected_);
    GPR_ASSERT(actual_addresses->size() == expected_addresses->size());
    for (size_t i = 0; i < expected_addresses->size(); ++i) {
      GPR_ASSERT((*actual_addresses)[i] == (*expected_addresses)[i]);
    }
    grpc_channel_args_destroy(args);
    grpc_channel_args_destroy(expected_);
    expected_ = nullptr;
    gpr_event_set(ev_, (void*)1);
    ev_ = nullptr;
  }

  void ReturnError(grpc_error* error) override {}

 private:
  grpc_channel_args* expected_ = nullptr;
  gpr_event* ev_ = nullptr;
};

static grpc_core::OrphanablePtr<grpc_core::Resolver> build_fake_resolver(
    grpc_combiner* combiner,
    grpc_core::FakeResolverResponseGenerator* response_generator,
    grpc_core::UniquePtr<grpc_core::Resolver::ResultHandler> result_handler) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("fake");
  grpc_arg generator_arg =
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator);
  grpc_channel_args channel_args = {1, &generator_arg};
  grpc_core::ResolverArgs args;
  args.args = &channel_args;
  args.combiner = combiner;
  args.result_handler = std::move(result_handler);
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  return resolver;
}

// Create a new resolution containing 2 addresses.
static grpc_channel_args* create_new_resolver_result() {
  static size_t test_counter = 0;
  const size_t num_addresses = 2;
  char* uri_string;
  char* balancer_name;
  // Create address list.
  grpc_core::ServerAddressList addresses;
  for (size_t i = 0; i < num_addresses; ++i) {
    gpr_asprintf(&uri_string, "ipv4:127.0.0.1:100%" PRIuPTR,
                 test_counter * num_addresses + i);
    grpc_uri* uri = grpc_uri_parse(uri_string, true);
    gpr_asprintf(&balancer_name, "balancer%" PRIuPTR,
                 test_counter * num_addresses + i);
    grpc_resolved_address address;
    GPR_ASSERT(grpc_parse_uri(uri, &address));
    grpc_core::InlinedVector<grpc_arg, 2> args_to_add;
    const bool is_balancer = num_addresses % 2;
    if (is_balancer) {
      args_to_add.emplace_back(grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BALANCER), 1));
      args_to_add.emplace_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_BALANCER_NAME), balancer_name));
    }
    grpc_channel_args* args = grpc_channel_args_copy_and_add(
        nullptr, args_to_add.data(), args_to_add.size());
    addresses.emplace_back(address.addr, address.len, args);
    gpr_free(balancer_name);
    grpc_uri_destroy(uri);
    gpr_free(uri_string);
  }
  // Embed the address list in channel args.
  const grpc_arg addresses_arg = CreateServerAddressListChannelArg(&addresses);
  grpc_channel_args* results =
      grpc_channel_args_copy_and_add(nullptr, &addresses_arg, 1);
  ++test_counter;
  return results;
}

static void test_fake_resolver() {
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  ResultHandler* result_handler = grpc_core::New<ResultHandler>();
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator =
          grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver = build_fake_resolver(
      combiner, response_generator.get(),
      grpc_core::UniquePtr<grpc_core::Resolver::ResultHandler>(result_handler));
  GPR_ASSERT(resolver.get() != nullptr);
  resolver->StartLocked();
  // Test 1: normal resolution.
  // next_results != NULL, reresolution_results == NULL.
  // Expected response is next_results.
  gpr_log(GPR_INFO, "TEST 1");
  grpc_channel_args* results = create_new_resolver_result();
  gpr_event ev1;
  gpr_event_init(&ev1);
  result_handler->SetExpectedAndEvent(results, &ev1);
  response_generator->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&ev1, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
  grpc_channel_args_destroy(results);
  // Test 2: update resolution.
  // next_results != NULL, reresolution_results == NULL.
  // Expected response is next_results.
  gpr_log(GPR_INFO, "TEST 2");
  results = create_new_resolver_result();
  gpr_event ev2;
  gpr_event_init(&ev2);
  result_handler->SetExpectedAndEvent(results, &ev2);
  response_generator->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&ev2, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
  grpc_channel_args_destroy(results);
  // Test 3: normal re-resolution.
  // next_results == NULL, reresolution_results != NULL.
  // Expected response is reresolution_results.
  gpr_log(GPR_INFO, "TEST 3");
  grpc_channel_args* reresolution_results = create_new_resolver_result();
  gpr_event ev3;
  gpr_event_init(&ev3);
  result_handler->SetExpectedAndEvent(reresolution_results, &ev3);
  // Set reresolution_results.
  // No result will be returned until re-resolution is requested.
  response_generator->SetReresolutionResponse(reresolution_results);
  grpc_core::ExecCtx::Get()->Flush();
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&ev3, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
  // Test 4: repeat re-resolution.
  // next_results == NULL, reresolution_results != NULL.
  // Expected response is reresolution_results.
  gpr_log(GPR_INFO, "TEST 4");
  gpr_event ev4;
  gpr_event_init(&ev4);
  result_handler->SetExpectedAndEvent(reresolution_results, &ev4);
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&ev4, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
  grpc_channel_args_destroy(reresolution_results);
  // Test 5: normal resolution.
  // next_results != NULL, reresolution_results != NULL.
  // Expected response is next_results.
  gpr_log(GPR_INFO, "TEST 5");
  results = create_new_resolver_result();
  gpr_event ev5;
  gpr_event_init(&ev5);
  result_handler->SetExpectedAndEvent(results, &ev5);
  response_generator->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&ev5, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
  grpc_channel_args_destroy(results);
  // Test 6: no-op.
  // Requesting a new resolution without setting the response shouldn't trigger
  // the resolution callback.
  gpr_log(GPR_INFO, "TEST 6");
  gpr_event ev6;
  gpr_event_init(&ev6);
  result_handler->SetExpectedAndEvent(nullptr, &ev6);
  GPR_ASSERT(gpr_event_wait(&ev6, grpc_timeout_milliseconds_to_deadline(100)) ==
             nullptr);
  // Clean up.
  resolver.reset();
  GRPC_COMBINER_UNREF(combiner, "test_fake_resolver");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  test_fake_resolver();

  grpc_shutdown();
  return 0;
}
