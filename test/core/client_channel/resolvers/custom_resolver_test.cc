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

#include <gtest/gtest.h>

grpc_lb_addresses* grpc_addresses_copy_lb_addresses_for_test(
    grpc_addresses* addresses);

namespace grpc {
namespace testing {
namespace {

class AddressesDelete {
 public:
  void operator()(grpc_addresses* addresses) {
    grpc_addresses_destroy(addresses);
  }
};

typedef std::unique_ptr<grpc_addresses, AddressesDelete> Addresses;

typedef struct on_resolution_arg {
  grpc_channel_args* resolver_result;
  grpc_lb_addresses* expected_resolver_result;
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
      res->expected_resolver_result;
  GPR_ASSERT(
      grpc_lb_addresses_cmp(actual_lb_addresses, expected_lb_addresses) == 0);
  grpc_channel_args_destroy(res->resolver_result);
  grpc_lb_addresses_destroy(res->expected_resolver_result);
  gpr_event_set(&res->ev, (void*)1);
}

// Create a new resolution containing 2 addresses.
static Addresses create_new_resolver_result(size_t index) {
  const size_t num_addresses = 2;
  char* uri_string;
  char* balancer_name;
  // Create grpc_addresses.
  Addresses addresses(grpc_addresses_create(num_addresses, nullptr));
  for (size_t i = 0; i < num_addresses; ++i) {
    gpr_asprintf(&uri_string, "ipv4:127.0.0.1:100%" PRIuPTR,
                 index * num_addresses + i);
    gpr_asprintf(&balancer_name, "balancer%" PRIuPTR,
                 index * num_addresses + i);
    if (num_addresses % 2 == 0) {
      grpc_addresses_add_balancer_address(addresses.get(), uri_string,
                                          balancer_name);
    } else {
      grpc_addresses_add_direct_address(addresses.get(), uri_string);
    }
    gpr_free(balancer_name);
    gpr_free(uri_string);
  }
  return addresses;
}

static on_resolution_arg create_on_resolution_arg(const Addresses& results) {
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_resolver_result =
      grpc_addresses_copy_lb_addresses_for_test(results.get());
  gpr_event_init(&on_res_arg.ev);
  return on_res_arg;
}

class Resolver {
 public:
  explicit Resolver(grpc_resolver_observer* observer) : observer_(observer) {}
  virtual ~Resolver() { grpc_resolver_observer_destroy(observer_); }

  virtual void RequestReresolution() = 0;

  static void RequestReresolutionWrapper(void* user_data) {
    static_cast<Resolver*>(user_data)->RequestReresolution();
  }
  static void DestroyWrapper(void* user_data) {
    delete static_cast<Resolver*>(user_data);
  }

 protected:
  void SetAddresses(const Addresses& addresses) {
    grpc_resolver_observer_set_addresses(observer_, addresses.get());
  }

 private:
  grpc_resolver_observer* observer_;
  grpc_addresses* reresolution_response_ = nullptr;
};

class ResolverFactory {
 public:
  ResolverFactory() {}
  virtual ~ResolverFactory() {}

  virtual Resolver* Resolve(grpc_resolver_args* args,
                            grpc_resolver_observer* observer) = 0;

  static void* ResolveWrapper(void* user_data, grpc_resolver_args* args,
                              grpc_resolver_observer* observer) {
    return static_cast<ResolverFactory*>(user_data)->Resolve(args, observer);
  }
  static void DestroyWrapper(void* user_data) {
    delete static_cast<ResolverFactory*>(user_data);
  }
};

class FakeResolver : public Resolver {
 public:
  FakeResolver(grpc_resolver_args* args, grpc_resolver_observer* observer)
      : Resolver(observer) {}

  void RequestReresolution() {
    if (reresolution_response_ != nullptr) {
      SetAddresses(reresolution_response_);
    }
  }

  void SetResponse(const Addresses& addresses) { SetAddresses(addresses); }

  void SetReresolutionResponse(Addresses addresses) {
    reresolution_response_ = std::move(addresses);
  }

 private:
  Addresses reresolution_response_ = nullptr;
};

class FakeResolverFactory : public ResolverFactory {
 public:
  FakeResolverFactory(FakeResolver** resolver) : resolver_(resolver) {}

  Resolver* Resolve(grpc_resolver_args* args,
                    grpc_resolver_observer* observer) {
    GPR_ASSERT(resolver_ != nullptr);
    auto resolver = new FakeResolver(args, observer);
    *resolver_ = resolver;
    resolver_ = nullptr;
    return resolver;
  }

 private:
  FakeResolver** resolver_;
};

static grpc_core::OrphanablePtr<grpc_core::Resolver> build_custom_resolver(
    const char* scheme, grpc_combiner* combiner) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory(scheme);
  grpc_channel_args channel_args = {0, nullptr};
  grpc_core::ResolverArgs args;
  args.args = &channel_args;
  args.combiner = combiner;
  return factory->CreateResolver(args);
}

static void register_resolver_factory(const char* scheme,
                                      ResolverFactory* factory) {
  grpc_resolver_factory_register(
      scheme, factory, ResolverFactory::ResolveWrapper,
      ResolverFactory::DestroyWrapper, Resolver::RequestReresolutionWrapper,
      Resolver::DestroyWrapper, nullptr);
}

TEST(CustomResolverTest, End2End) {
  FakeResolver* fake_resolver = nullptr;
  register_resolver_factory("custom", new FakeResolverFactory(&fake_resolver));

  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("custom", combiner);
  GPR_ASSERT(resolver.get() != nullptr);
  GPR_ASSERT(fake_resolver != nullptr);
  // Test 1: normal resolution.
  // next_results != NULL, reresolution_results == NULL.
  // Expected response is next_results.
  Addresses results = create_new_resolver_result(1);
  on_resolution_arg on_res_arg = create_on_resolution_arg(results);
  grpc_closure* on_resolution = GRPC_CLOSURE_CREATE(
      on_resolution_cb, &on_res_arg, grpc_combiner_scheduler(combiner));
  // Resolution won't be triggered until next_results is set.
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  fake_resolver->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 2: update resolution.
  // next_results != NULL, reresolution_results == NULL.
  // Expected response is next_results.
  results = create_new_resolver_result(2);
  on_res_arg = create_on_resolution_arg(results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  // Resolution won't be triggered until next_results is set.
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  fake_resolver->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 3: normal re-resolution.
  // next_results == NULL, reresolution_results != NULL.
  // Expected response is reresolution_results.
  Addresses reresolution_results = create_new_resolver_result(3);
  on_res_arg = create_on_resolution_arg(reresolution_results);
  on_resolution_arg on_reresolution_res_arg =
      create_on_resolution_arg(reresolution_results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  // Set reresolution_results.
  fake_resolver->SetReresolutionResponse(std::move(reresolution_results));
  // Flush here to guarantee that the response has been set.
  grpc_core::ExecCtx::Get()->Flush();
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 4: repeat re-resolution.
  // next_results == NULL, reresolution_results != NULL.
  // Expected response is reresolution_results.
  on_resolution =
      GRPC_CLOSURE_CREATE(on_resolution_cb, &on_reresolution_res_arg,
                          grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_reresolution_res_arg.resolver_result, on_resolution);
  // Trigger a re-resolution.
  resolver->RequestReresolutionLocked();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 5: normal resolution.
  // next_results != NULL, reresolution_results != NULL.
  // Expected response is next_results.
  results = create_new_resolver_result(4);
  on_res_arg = create_on_resolution_arg(results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  // Resolution won't be triggered until next_results is set.
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  fake_resolver->SetResponse(results);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 6: multiple updates.
  // If SetAddresses is called multiple times the last one wins.
  results = create_new_resolver_result(5);
  fake_resolver->SetResponse(results);
  results = create_new_resolver_result(6);
  fake_resolver->SetResponse(results);
  results = create_new_resolver_result(7);
  fake_resolver->SetResponse(results);
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg = create_on_resolution_arg(results);
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_res_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&on_res_arg.resolver_result, on_resolution);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_res_arg.ev,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  // Test 7: no-op.
  // Requesting a new resolution without setting the response shouldn't
  // trigger the resolution callback.
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
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

class ZeroResolverFactory : public ResolverFactory {
 public:
  virtual Resolver* Resolve(grpc_resolver_args* args,
                            grpc_resolver_observer* observer) {
    return nullptr;
  }
};

TEST(CustomResolverTest, Failure) {
  register_resolver_factory("zero", new ZeroResolverFactory());

  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("zero", combiner);
  GPR_ASSERT(resolver.get() == nullptr);

  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_test_init(argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
