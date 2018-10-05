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

class ResolverPlugin : public grpc_resolver {
 public:
  ResolverPlugin() {
    next = NextWrapper;
    request_reresolution = RequestReresolutionWrapper;
    shutdown = ShutdownWrapper;
    destroy = DestroyWrapper;
  }
  virtual ~ResolverPlugin() {}

  void Next(grpc_resolver_next_cb cb, void* user_data) {
    next_cb_ = cb;
    next_user_data_ = user_data;
  }
  virtual void RequestReresolution() {}
  virtual void Shutdown() {
    if (next_cb_ != nullptr) {
      next_cb_(next_user_data_, nullptr, "Shutdown");
      next_cb_ = nullptr;
    }
  }

  bool HasNextCallback() const { return next_cb_ != nullptr; }

  void SetResult(const grpc_resolver_result* result) {
    next_cb_(next_user_data_, result, nullptr);
    next_cb_ = nullptr;
  }

  void SetError(const char* error_details) {
    next_cb_(next_user_data_, nullptr, error_details);
    next_cb_ = nullptr;
  }

 private:
  static void NextWrapper(grpc_resolver* resolver, grpc_resolver_next_cb cb,
                          void* user_data) {
    static_cast<ResolverPlugin*>(resolver)->Next(cb, user_data);
  }
  static void RequestReresolutionWrapper(grpc_resolver* resolver) {
    static_cast<ResolverPlugin*>(resolver)->RequestReresolution();
  }
  static void ShutdownWrapper(grpc_resolver* resolver) {
    static_cast<ResolverPlugin*>(resolver)->Shutdown();
  }
  static void DestroyWrapper(grpc_resolver* resolver) {
    grpc_core::Delete(static_cast<ResolverPlugin*>(resolver));
  }

  grpc_resolver_next_cb next_cb_ = nullptr;
  void* next_user_data_ = nullptr;
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
    delete static_cast<ResolverPluginFactory*>(factory);
  }
};

class AsyncResolverPluginFactory : public grpc_resolver_factory {
 public:
  AsyncResolverPluginFactory() {
    create_resolver = CreateResolverWrapper;
    destroy = DestroyWrapper;
  }
  virtual ~AsyncResolverPluginFactory() {}

  void SetResolver(ResolverPlugin* resolver, const char* error_details) {
    cb_(user_data_, resolver, error_details);
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
    return 0;
  }
  static void DestroyWrapper(grpc_resolver_factory* factory) {
    delete static_cast<AsyncResolverPluginFactory*>(factory);
  }

 private:
  grpc_resolver_creation_cb cb_;
  void* user_data_;
};

grpc_core::OrphanablePtr<grpc_core::Resolver> build_custom_resolver(
    const char* scheme, grpc_combiner* combiner) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory(scheme);
  grpc_core::ResolverArgs args;
  args.combiner = combiner;
  return factory->CreateResolver(args);
}

class ResultPropagationResolver : public ResolverPlugin {
 public:
};

class ResultPropagationResolverFactory : public ResolverPluginFactory {
 public:
  ResultPropagationResolverFactory(ResultPropagationResolver** resolver)
      : resolver_(resolver) {}
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    *resolver_ = new ResultPropagationResolver();
    return *resolver_;
  }

 private:
  ResultPropagationResolver** resolver_;
};

struct OnResolutionArgs {
  OnResolutionArgs() { gpr_event_init(&event); }
  ~OnResolutionArgs() { GRPC_ERROR_UNREF(error); }
  gpr_event event;
  grpc_error* error;
};

void on_resolution_cb(void* arg, grpc_error* error) {
  OnResolutionArgs* res = static_cast<OnResolutionArgs*>(arg);
  res->error = GRPC_ERROR_REF(error);
  gpr_event_set(&res->event, (void*)1);
}

void expect_error(const grpc_core::OrphanablePtr<grpc_core::Resolver>& resolver,
                  const char* expected_error_details, grpc_combiner* combiner) {
  grpc_channel_args* channel_args = nullptr;
  OnResolutionArgs on_resolution_arg;
  grpc_closure* on_resolution = GRPC_CLOSURE_CREATE(
      on_resolution_cb, &on_resolution_arg, grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_resolution_arg.event,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  ASSERT_NE(on_resolution_arg.error, nullptr);
  ASSERT_EQ(channel_args, nullptr);
  grpc_slice desc;
  ASSERT_TRUE(grpc_error_get_str(on_resolution_arg.error,
                                 GRPC_ERROR_STR_DESCRIPTION, &desc));
  ASSERT_EQ(grpc_slice_str_cmp(desc, expected_error_details), 0);
}

TEST(PluginResolverTest, ResultPropagation) {
  ResultPropagationResolver* plugin_resolver = nullptr;
  auto* factory = new ResultPropagationResolverFactory(&plugin_resolver);
  grpc_resolver_factory_register("result_propagation", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("result_propagation", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  ASSERT_NE(plugin_resolver, nullptr);
  // Check happy path.
  ASSERT_FALSE(plugin_resolver->HasNextCallback());
  OnResolutionArgs on_resolution_arg;
  grpc_channel_args* channel_args = nullptr;
  grpc_closure* on_resolution = GRPC_CLOSURE_CREATE(
      on_resolution_cb, &on_resolution_arg, grpc_combiner_scheduler(combiner));
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
  GPR_ASSERT(gpr_event_wait(&on_resolution_arg.event,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  ASSERT_EQ(on_resolution_arg.error, nullptr);
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
  ASSERT_EQ(addrs->num_addresses, 1);
  grpc_resolved_address expected_address;
  ASSERT_TRUE(
      grpc_parse_ipv4_hostport("127.0.0.1:10", &expected_address, true));
  ASSERT_EQ(memcmp(&addrs->addresses[0].address, &expected_address,
                   sizeof(expected_address)),
            0);
  grpc_channel_args_destroy(channel_args);
  // Check failure path.
  channel_args = nullptr;
  on_resolution_arg = OnResolutionArgs();
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_resolution_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
  plugin_resolver->SetError("custom error");
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&on_resolution_arg.event,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  ASSERT_NE(on_resolution_arg.error, nullptr);
  grpc_slice desc;
  ASSERT_TRUE(grpc_error_get_str(on_resolution_arg.error,
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
  explicit RequestReresolutionCounterResolver(size_t* counter)
      : counter_(counter) {}
  void RequestReresolution() override { (*counter_)++; }

 private:
  size_t* counter_;
};

class RequestReresolutionCounterResolverFactory : public ResolverPluginFactory {
 public:
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    return new RequestReresolutionCounterResolver(&counter_);
  }
  size_t GetCounter() { return counter_; }

 private:
  ResolverPlugin* resolver_;
  size_t counter_ = 0;
};

TEST(PluginResolverTest, RequestReresolution) {
  auto* factory = new RequestReresolutionCounterResolverFactory();
  grpc_resolver_factory_register("request_reresolution_counter", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("request_reresolution_counter", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  ASSERT_EQ(factory->GetCounter(), 0);
  resolver->RequestReresolutionLocked();
  ASSERT_EQ(factory->GetCounter(), 1);
  resolver->RequestReresolutionLocked();
  ASSERT_EQ(factory->GetCounter(), 2);
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

class TargetResolverFactory : public ResolverPluginFactory {
 public:
  TargetResolverFactory(grpc_core::UniquePtr<char>* target) : target_(target) {}
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    target_->reset(gpr_strdup(args->target_uri));
    return grpc_core::New<ResolverPlugin>();
  }

 private:
  grpc_core::UniquePtr<char>* target_;
};

TEST(PluginResolverTest, TargetPropagation) {
  grpc_core::UniquePtr<char> target;
  grpc_resolver_factory_register("target", new TargetResolverFactory(&target));
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
    delete static_cast<FailToInstantiateResolverFactory*>(factory);
  }
};

TEST(PluginResolverTest, FailToInstantiateResolver) {
  auto* factory = new FailToInstantiateResolverFactory();
  grpc_resolver_factory_register("failure-to-instantiate", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("failure-to-instantiate", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  for (size_t i = 0; i < 3; i++) {
    expect_error(resolver, "failed to resolve", combiner);
  }
  resolver.reset();
  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

TEST(PluginResolverTest, FailToInstantiateResolverAsync) {
  auto* factory = new AsyncResolverPluginFactory();
  grpc_resolver_factory_register("failure-to-instantiate-async", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Request resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("failure-to-instantiate-async", combiner);
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
  auto* factory = new AsyncResolverPluginFactory();
  grpc_resolver_factory_register("shutdown-before-creation-is-done", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Request resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("shutdown-before-creation-is-done", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  resolver.reset();
  factory->SetResolver(grpc_core::New<ResolverPlugin>(), nullptr);
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
