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
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"

#include "test/core/util/test_config.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

struct ObserverDeleter {
  void operator()(grpc_resolver_observer* observer) {
    grpc_resolver_observer_unref(observer);
  }
};

typedef std::unique_ptr<grpc_resolver_observer, ObserverDeleter> ObserverPtr;

ObserverPtr MakeObserverPtr(grpc_resolver_observer* observer) {
  grpc_resolver_observer_ref(observer);
  return ObserverPtr(observer);
}

class ResolverPlugin : public grpc_resolver {
 public:
  ResolverPlugin(grpc_resolver_observer* observer)
      : ResolverPlugin(MakeObserverPtr(observer)) {}
  ResolverPlugin(ObserverPtr observer) : observer_(std::move(observer)) {
    request_reresolution = RequestReresolutionWrapper;
    destroy = DestroyWrapper;
  }
  virtual ~ResolverPlugin() {}

  virtual void RequestReresolution() {}

  void SetResult(const grpc_resolver_result* result) {
    grpc_resolver_observer_set_result(observer_.get(), result);
  }

  void SetError(const char* error_details) {
    grpc_resolver_observer_set_error(observer_.get(), error_details);
  }

 private:
  static void RequestReresolutionWrapper(grpc_resolver* resolver) {
    static_cast<ResolverPlugin*>(resolver)->RequestReresolution();
  }
  static void DestroyWrapper(grpc_resolver* resolver) {
    grpc_core::Delete(static_cast<ResolverPlugin*>(resolver));
  }

  ObserverPtr observer_;
};  // namespace

class ResolverPluginFactory : public grpc_resolver_factory {
 public:
  ResolverPluginFactory() {
    create_resolver = CreateResolverWrapper;
    destroy = DestroyWrapper;
  }
  virtual ~ResolverPluginFactory() {}

  virtual ResolverPlugin* Resolve(grpc_resolver_args* args) = 0;

 private:
  static int CreateResolverWrapper(grpc_resolver_factory* factory,
                                   grpc_resolver_args* args,
                                   grpc_resolver_creation_cb cb,
                                   void* user_data, grpc_resolver** resolver,
                                   const char** error_details) {
    *resolver = static_cast<ResolverPluginFactory*>(factory)->Resolve(args);
    return 1;
  }
  static void DestroyWrapper(grpc_resolver_factory* factory) {
    grpc_core::Delete(static_cast<ResolverPluginFactory*>(factory));
  }
};

class AsyncResolverPluginFactory : public grpc_resolver_factory {
 public:
  AsyncResolverPluginFactory() {
    create_resolver = CreateResolverWrapper;
    destroy = DestroyWrapper;
  }
  virtual ~AsyncResolverPluginFactory() {}

  ObserverPtr TakeObserver() {
    GPR_ASSERT(observer_ != nullptr);
    return std::move(observer_);
  }
  void SetResolver(ResolverPlugin* resolver, const char* error_details) {
    cb_(user_data_, resolver, error_details);
    cb_ = nullptr;
  }

 private:
  static int CreateResolverWrapper(grpc_resolver_factory* factory,
                                   grpc_resolver_args* args,
                                   grpc_resolver_creation_cb cb,
                                   void* user_data, grpc_resolver** resolver,
                                   const char** error_details) {
    auto* async_factory = static_cast<AsyncResolverPluginFactory*>(factory);
    async_factory->cb_ = cb;
    async_factory->user_data_ = user_data;
    async_factory->observer_ = MakeObserverPtr(args->observer);
    return 0;
  }
  static void DestroyWrapper(grpc_resolver_factory* factory) {
    grpc_core::Delete(static_cast<AsyncResolverPluginFactory*>(factory));
  }

 private:
  ObserverPtr observer_;
  grpc_resolver_creation_cb cb_ = nullptr;
  void* user_data_;
};

grpc_core::OrphanablePtr<grpc_core::Resolver> CreatePluginResolver(
    const char* scheme, grpc_combiner* combiner) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory(scheme);
  grpc_core::ResolverArgs args;
  args.combiner = combiner;
  return factory->CreateResolver(args);
}

class ResultPropagationResolver : public ResolverPlugin {
 public:
  ResultPropagationResolver(const grpc_resolver_args* args)
      : ResolverPlugin(args->observer) {}
};

class ResultPropagationResolverFactory : public ResolverPluginFactory {
 public:
  ResultPropagationResolverFactory(ResultPropagationResolver** resolver)
      : resolver_(resolver) {}
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    *resolver_ = grpc_core::New<ResultPropagationResolver>(args);
    return *resolver_;
  }

 private:
  ResultPropagationResolver** resolver_;
};

struct OnResolutionArgs {
  OnResolutionArgs() { gpr_event_init(&event); }
  ~OnResolutionArgs() { GRPC_ERROR_UNREF(error); }
  gpr_event event;
  grpc_error* error = nullptr;
};

void OnResolutionCallback(void* arg, grpc_error* error) {
  OnResolutionArgs* res = static_cast<OnResolutionArgs*>(arg);
  res->error = GRPC_ERROR_REF(error);
  gpr_event_set(&res->event, (void*)1);
}

void expect_error(const grpc_core::OrphanablePtr<grpc_core::Resolver>& resolver,
                  const char* expected_error_details, grpc_combiner* combiner) {
  grpc_channel_args* channel_args = nullptr;
  auto on_resolution_arg = grpc_core::MakeUnique<OnResolutionArgs>();
  grpc_closure* on_resolution =
      GRPC_CLOSURE_CREATE(OnResolutionCallback, on_resolution_arg.get(),
                          grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_resolution_arg->event,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  ASSERT_NE(on_resolution_arg->error, nullptr);
  ASSERT_EQ(channel_args, nullptr);
  grpc_slice desc;
  ASSERT_TRUE(grpc_error_get_str(on_resolution_arg->error,
                                 GRPC_ERROR_STR_DESCRIPTION, &desc));
  ASSERT_EQ(grpc_slice_str_cmp(desc, expected_error_details), 0);
}

TEST(PluginResolverTest, ResultPropagation) {
  ResultPropagationResolver* plugin_resolver = nullptr;
  auto* factory =
      grpc_core::New<ResultPropagationResolverFactory>(&plugin_resolver);
  grpc_resolver_factory_register("result_propagation", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      CreatePluginResolver("result_propagation", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  ASSERT_NE(plugin_resolver, nullptr);
  // Check happy path.
  auto on_resolution_arg = grpc_core::MakeUnique<OnResolutionArgs>();
  grpc_channel_args* channel_args = nullptr;
  grpc_closure* on_resolution =
      GRPC_CLOSURE_CREATE(OnResolutionCallback, on_resolution_arg.get(),
                          grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
  grpc_core::InlinedVector<grpc_address, 1> addresses;
  addresses.emplace_back(grpc_address{"ipv4:127.0.0.1:10", false, nullptr});
  grpc_resolver_result result;
  memset(&result, 0, sizeof(result));
  result.json_service_config = "{\"foo\": \"boo\"}";
  result.num_addresses = 1;
  result.addresses = addresses.data();
  plugin_resolver->SetResult(&result);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_resolution_arg->event,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  ASSERT_EQ(on_resolution_arg->error, nullptr);
  ASSERT_NE(channel_args, nullptr);
  auto* service_config_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_SERVICE_CONFIG);
  ASSERT_NE(service_config_arg, nullptr);
  ASSERT_EQ(strcmp(grpc_channel_arg_get_string(service_config_arg),
                   result.json_service_config),
            0);
  auto* lb_addresses_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_LB_ADDRESSES);
  ASSERT_NE(lb_addresses_arg, nullptr);
  grpc_lb_addresses* addrs =
      static_cast<grpc_lb_addresses*>(lb_addresses_arg->value.pointer.p);
  ASSERT_EQ(addrs->num_addresses, 1u);
  grpc_resolved_address expected_address;
  ASSERT_TRUE(
      grpc_parse_ipv4_hostport("127.0.0.1:10", &expected_address, true));
  ASSERT_EQ(memcmp(&addrs->addresses[0].address, &expected_address,
                   sizeof(expected_address)),
            0);
  grpc_channel_args_destroy(channel_args);
  // Check failure path.
  channel_args = nullptr;
  on_resolution_arg = grpc_core::MakeUnique<OnResolutionArgs>();
  on_resolution =
      GRPC_CLOSURE_CREATE(OnResolutionCallback, on_resolution_arg.get(),
                          grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
  plugin_resolver->SetError("custom error");
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_resolution_arg->event,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  ASSERT_NE(on_resolution_arg->error, nullptr);
  grpc_slice desc;
  ASSERT_TRUE(grpc_error_get_str(on_resolution_arg->error,
                                 GRPC_ERROR_STR_DESCRIPTION, &desc));
  ASSERT_EQ(grpc_slice_str_cmp(desc, "custom error"), 0);
  ASSERT_EQ(channel_args, nullptr);
  grpc_channel_args_destroy(channel_args);
  // Cleanup.
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

class RequestReresolutionCounterResolver : public ResolverPlugin {
 public:
  explicit RequestReresolutionCounterResolver(const grpc_resolver_args* args,
                                              size_t* counter)
      : ResolverPlugin(args->observer), counter_(counter) {}
  void RequestReresolution() override { (*counter_)++; }

 private:
  size_t* counter_;
};

class RequestReresolutionCounterResolverFactory : public ResolverPluginFactory {
 public:
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    return grpc_core::New<RequestReresolutionCounterResolver>(args, &counter_);
  }
  size_t GetCounter() { return counter_; }

 private:
  size_t counter_ = 0;
};

TEST(PluginResolverTest, RequestReresolution) {
  auto* factory = grpc_core::New<RequestReresolutionCounterResolverFactory>();
  grpc_resolver_factory_register("request_reresolution_counter", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      CreatePluginResolver("request_reresolution_counter", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  ASSERT_EQ(factory->GetCounter(), 0u);
  resolver->RequestReresolutionLocked();
  ASSERT_EQ(factory->GetCounter(), 1u);
  // Reresolution attempts are batched.
  for (size_t i = 0; i < 10; i++) {
    resolver->RequestReresolutionLocked();
    ASSERT_EQ(factory->GetCounter(), 1u);
  }
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_millis(1500, GPR_TIMESPAN)));
  ASSERT_EQ(factory->GetCounter(), 2u);
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

class TargetResolverFactory : public ResolverPluginFactory {
 public:
  TargetResolverFactory(grpc_core::UniquePtr<char>* target) : target_(target) {}
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    target_->reset(gpr_strdup(args->target_uri));
    return grpc_core::New<ResolverPlugin>(args->observer);
  }

 private:
  grpc_core::UniquePtr<char>* target_;
};

TEST(PluginResolverTest, TargetPropagation) {
  grpc_core::UniquePtr<char> target;
  grpc_resolver_factory_register(
      "target", grpc_core::New<TargetResolverFactory>(&target));
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("target");
  grpc_core::ResolverArgs args;
  args.combiner = combiner;
  args.target = "custom target";
  auto resolver = factory->CreateResolver(args);
  ASSERT_EQ(strcmp(target.get(), "custom target"), 0);
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

class FailToInstantiateResolverFactory : public grpc_resolver_factory {
 public:
  FailToInstantiateResolverFactory() {
    create_resolver = CreateResolverWrapper;
    destroy = DestroyWrapper;
  }

 private:
  static int CreateResolverWrapper(grpc_resolver_factory* factory,
                                   grpc_resolver_args* args,
                                   grpc_resolver_creation_cb cb,
                                   void* user_data, grpc_resolver** resolver,
                                   const char** error_details) {
    *error_details = gpr_strdup("failed to resolve");
    return 1;
  }
  static void DestroyWrapper(grpc_resolver_factory* factory) {
    grpc_core::Delete(static_cast<FailToInstantiateResolverFactory*>(factory));
  }
};

TEST(PluginResolverTest, FailToInstantiateResolver) {
  auto* factory = grpc_core::New<FailToInstantiateResolverFactory>();
  grpc_resolver_factory_register("failure-to-instantiate", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      CreatePluginResolver("failure-to-instantiate", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  for (size_t i = 0; i < 3; i++) {
    expect_error(resolver, "failed to resolve", combiner);
  }
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

TEST(PluginResolverTest, FailToInstantiateResolverAsync) {
  auto* factory = grpc_core::New<AsyncResolverPluginFactory>();
  grpc_resolver_factory_register("failure-to-instantiate-async", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Request resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      CreatePluginResolver("failure-to-instantiate-async", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  // Fail its creation.
  factory->SetResolver(nullptr, "bad test");
  for (size_t i = 0; i < 3; i++) {
    expect_error(resolver, "bad test", combiner);
  }
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

TEST(PluginResolverTest, ShutdownBeforeCreationIsDone) {
  auto* factory = grpc_core::New<AsyncResolverPluginFactory>();
  grpc_resolver_factory_register("shutdown-before-creation-is-done", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Request resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      CreatePluginResolver("shutdown-before-creation-is-done", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  resolver.reset();
  factory->SetResolver(grpc_core::New<ResolverPlugin>(factory->TakeObserver()),
                       nullptr);
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
