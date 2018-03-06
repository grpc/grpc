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

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"

#include "test/core/util/test_config.h"

static grpc_core::OrphanablePtr<grpc_core::Resolver> build_fake_resolver(
    grpc_combiner* combiner,
    grpc_core::FakeResolverResponseGenerator* response_generator) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("fake");
  grpc_arg generator_arg =
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator);
  grpc_channel_args channel_args = {1, &generator_arg};
  grpc_core::ResolverArgs args;
  args.args = &channel_args;
  args.combiner = combiner;
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(args);
  return resolver;
}

typedef struct on_resolution_arg {
  grpc_channel_args* resolver_result;
  grpc_channel_args* expected_resolver_result;
  gpr_event ev;
} on_resolution_arg;

// Callback to check the resolution result is as expected.
void on_resolution_cb(void* arg, grpc_error* error) {
  if (error != GRPC_ERROR_NONE) return;
  on_resolution_arg* res = static_cast<on_resolution_arg*>(arg);
  // We only check the addresses channel arg because that's the only one
  // explicitly set by the test via
  // FakeResolverResponseGenerator::SetResponse().
  const grpc_lb_addresses* actual_lb_addresses =
      grpc_lb_addresses_find_channel_arg(res->resolver_result);
  const grpc_lb_addresses* expected_lb_addresses =
      grpc_lb_addresses_find_channel_arg(res->expected_resolver_result);
  GPR_ASSERT(
      grpc_lb_addresses_cmp(actual_lb_addresses, expected_lb_addresses) == 0);
  grpc_channel_args_destroy(res->resolver_result);
  grpc_channel_args_destroy(res->expected_resolver_result);
  gpr_event_set(&res->ev, (void*)1);
}

// Create a new resolution containing 2 addresses.
static grpc_channel_args* create_new_resolver_result() {
  static size_t test_counter = 0;
  const size_t num_addresses = 2;
  char* uri_string;
  char* balancer_name;
  // Create grpc_lb_addresses.
  grpc_lb_addresses* addresses =
      grpc_lb_addresses_create(num_addresses, nullptr);
  for (size_t i = 0; i < num_addresses; ++i) {
    gpr_asprintf(&uri_string, "ipv4:127.0.0.1:100%" PRIuPTR,
                 test_counter * num_addresses + i);
    grpc_uri* uri = grpc_uri_parse(uri_string, true);
    gpr_asprintf(&balancer_name, "balancer%" PRIuPTR,
                 test_counter * num_addresses + i);
    grpc_lb_addresses_set_address_from_uri(
        addresses, i, uri, bool(num_addresses % 2), balancer_name, nullptr);
    gpr_free(balancer_name);
    grpc_uri_destroy(uri);
    gpr_free(uri_string);
  }
  // Convert grpc_lb_addresses to grpc_channel_args.
  const grpc_arg addresses_arg =
      grpc_lb_addresses_create_channel_arg(addresses);
  grpc_channel_args* results =
      grpc_channel_args_copy_and_add(nullptr, &addresses_arg, 1);
  grpc_lb_addresses_destroy(addresses);
  ++test_counter;
  return results;
}

static on_resolution_arg create_on_resolution_arg(grpc_channel_args* results) {
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_resolver_result = results;
  gpr_event_init(&on_res_arg.ev);
  return on_res_arg;
}

static void test_fake_resolver() {
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator =
          grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_fake_resolver(combiner, response_generator.get());
  GPR_ASSERT(resolver.get() != nullptr);
  // Test 1: normal resolution.
  // next_results != NULL, reresolution_results == NULL, last_used_results ==
  // NULL. Expected response is next_results.
  grpc_channel_args* results = create_new_resolver_result();
  on_resolution_arg on_res_arg = create_on_resolution_arg(results);
  grpc_closure* on_resolution = GRPC_CLOSURE_CREATE(
      on_resolution_cb, &on_res_arg, grpc_combiner_scheduler(combiner));
  // Resolution won't be triggered until next_results is set.
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  response_generator->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 2: update resolution.
  // next_results != NULL, reresolution_results == NULL, last_used_results !=
  // NULL. Expected response is next_results.
  results = create_new_resolver_result();
  grpc_channel_args* last_used_results = grpc_channel_args_copy(results);
  on_res_arg = create_on_resolution_arg(results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  // Resolution won't be triggered until next_results is set.
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  response_generator->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 3: fallback re-resolution.
  // next_results == NULL, reresolution_results == NULL, last_used_results !=
  // NULL. Expected response is last_used_results.
  on_res_arg = create_on_resolution_arg(last_used_results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 4: normal re-resolution.
  // next_results == NULL, reresolution_results != NULL, last_used_results !=
  // NULL. Expected response is reresolution_results.
  grpc_channel_args* reresolution_results = create_new_resolver_result();
  on_res_arg =
      create_on_resolution_arg(grpc_channel_args_copy(reresolution_results));
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  // Set reresolution_results.
  response_generator->SetReresolutionResponse(reresolution_results);
  // Flush here to guarantee that the response has been set.
  grpc_core::ExecCtx::Get()->Flush();
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 5: repeat re-resolution.
  // next_results == NULL, reresolution_results != NULL, last_used_results !=
  // NULL. Expected response is reresolution_results.
  on_res_arg = create_on_resolution_arg(reresolution_results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 6: normal resolution.
  // next_results != NULL, reresolution_results != NULL, last_used_results !=
  // NULL. Expected response is next_results.
  results = create_new_resolver_result();
  last_used_results = grpc_channel_args_copy(results);
  on_res_arg = create_on_resolution_arg(results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  // Resolution won't be triggered until next_results is set.
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  response_generator->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 7: fallback re-resolution.
  // next_results == NULL, reresolution_results == NULL, last_used_results !=
  // NULL. Expected response is last_used_results.
  on_res_arg = create_on_resolution_arg(last_used_results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  // Reset reresolution_results.
  response_generator->SetReresolutionResponse(nullptr);
  // Flush here to guarantee that reresolution_results has been reset.
  grpc_core::ExecCtx::Get()->Flush();
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 8: no-op.
  // Requesting a new resolution without setting the response shouldn't trigger
  // the resolution callback.
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_milliseconds_to_deadline(100)) ==
             nullptr);
  // Clean up.
  // Note: Need to explicitly unref the resolver and flush the exec_ctx
  // to make sure that the final resolver callback (with error set to
  // "Resolver Shutdown") is invoked before on_res_arg goes out of scope.
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "test_fake_resolver");
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_fake_resolver();

  grpc_shutdown();
  return 0;
}
