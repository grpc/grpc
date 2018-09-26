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

class ResolverPlugin {
 public:
  explicit ResolverPlugin(grpc_resolver_observer* observer)
      : observer_(observer) {}
  virtual ~ResolverPlugin() { grpc_resolver_observer_destroy(observer_); }

  virtual void RequestReresolution() = 0;

  static void RequestReresolutionWrapper(void* user_data) {
    static_cast<ResolverPlugin*>(user_data)->RequestReresolution();
  }
  static void DestroyWrapper(void* user_data) {
    delete static_cast<ResolverPlugin*>(user_data);
  }

  void SetResult(const grpc_resolver_result* result) {
    grpc_resolver_observer_set_result(observer_, result);
  }

  void SetError(const char* desc) {
    grpc_resolver_observer_set_error(observer_, __FILE__, __LINE__,
                                     grpc_slice_from_static_string(desc));
  }

 private:
  grpc_resolver_observer* observer_;
};

class ResolverPluginFactory {
 public:
  ResolverPluginFactory() {}
  virtual ~ResolverPluginFactory() {}

  virtual ResolverPlugin* Resolve(grpc_resolver_args* args) = 0;

  static grpc_resolver CreateResolverWrapper(void* user_data,
                                             grpc_resolver_args* args) {
    grpc_resolver resolver;
    memset(&resolver, 0, sizeof(resolver));
    resolver.user_data =
        static_cast<ResolverPluginFactory*>(user_data)->Resolve(args);
    resolver.request_reresolution = ResolverPlugin::RequestReresolutionWrapper;
    resolver.destroy = ResolverPlugin::DestroyWrapper;
    return resolver;
  }
  static void DestroyWrapper(void* user_data) {
    delete static_cast<ResolverPluginFactory*>(user_data);
  }
};

grpc_core::OrphanablePtr<grpc_core::Resolver> build_custom_resolver(
    const char* scheme, grpc_combiner* combiner) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory(scheme);
  grpc_core::ResolverArgs args;
  args.combiner = combiner;
  return factory->CreateResolver(args);
}

void register_resolver_factory(const char* scheme,
                               ResolverPluginFactory* factory) {
  grpc_resolver_factory wrapper;
  memset(&wrapper, 0, sizeof(wrapper));
  wrapper.user_data = factory;
  wrapper.create_resolver = ResolverPluginFactory::CreateResolverWrapper;
  wrapper.destroy = ResolverPluginFactory::DestroyWrapper;

  grpc_resolver_factory_register(scheme, wrapper);
}

class ResultPropagationResolver : public ResolverPlugin {
 public:
  explicit ResultPropagationResolver(grpc_resolver_observer* observer)
      : ResolverPlugin(observer) {}
  void RequestReresolution() override {}
};

class ResultPropagationResolverFactory : public ResolverPluginFactory {
 public:
  ResultPropagationResolverFactory(ResultPropagationResolver** resolver)
      : resolver_(resolver) {}
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    *resolver_ = new ResultPropagationResolver(args->observer);
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

TEST(PluginResolverTest, ResultPropagation) {
  ResultPropagationResolver* plugin_resolver = nullptr;
  auto* factory = new ResultPropagationResolverFactory(&plugin_resolver);
  register_resolver_factory("result_propagation", factory);
  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("result_propagation", combiner);
  ASSERT_NE(resolver.get(), nullptr);
  ASSERT_NE(plugin_resolver, nullptr);
  // Check happy path.
  grpc_core::InlinedVector<grpc_address, 1> addresses;
  addresses.emplace_back(grpc_address{"ipv4:127.0.0.1:10", false, nullptr});
  grpc_resolver_result result;
  memset(&result, 0, sizeof(result));
  result.json_service_config = "{\"foo\": \"boo\"}";
  result.num_addresses = 1;
  result.addresses = addresses.data();
  plugin_resolver->SetResult(&result);
  OnResolutionArgs on_resolution_arg;
  grpc_channel_args* channel_args = nullptr;
  grpc_closure* on_resolution = GRPC_CLOSURE_CREATE(
      on_resolution_cb, &on_resolution_arg, grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
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
  plugin_resolver->SetError("custom error");
  on_resolution = GRPC_CLOSURE_CREATE(on_resolution_cb, &on_resolution_arg,
                                      grpc_combiner_scheduler(combiner));
  resolver->NextLocked(&channel_args, on_resolution);
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
  explicit RequestReresolutionCounterResolver(grpc_resolver_observer* observer,
                                              size_t* counter)
      : ResolverPlugin(observer), counter_(counter) {}
  void RequestReresolution() override { (*counter_)++; }

 private:
  size_t* counter_;
};

class RequestReresolutionCounterResolverFactory : public ResolverPluginFactory {
 public:
  ResolverPlugin* Resolve(grpc_resolver_args* args) override {
    return new RequestReresolutionCounterResolver(args->observer, &counter_);
  }
  size_t GetCounter() { return counter_; }

 private:
  ResolverPlugin* resolver_;
  size_t counter_ = 0;
};

TEST(PluginResolverTest, RequestReresolution) {
  auto* factory = new RequestReresolutionCounterResolverFactory();
  register_resolver_factory("request_reresolution_counter", factory);
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
    return nullptr;
  }

 private:
  grpc_core::UniquePtr<char>* target_;
};

TEST(PluginResolverTest, TargetPropagation) {
  grpc_core::UniquePtr<char> target;
  register_resolver_factory("target", new TargetResolverFactory(&target));

  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("target");
  grpc_core::ResolverArgs args;
  args.combiner = combiner;
  args.target = "custom target";
  auto resolver = factory->CreateResolver(args);
  ASSERT_EQ(resolver.get(), nullptr);
  ASSERT_EQ(strcmp(target.get(), "custom target"), 0);

  grpc_core::ExecCtx::Get()->Flush();
  GRPC_COMBINER_UNREF(combiner, "CustomResolverTest");
}

class ZeroResolverFactory : public ResolverPluginFactory {
 public:
  ResolverPlugin* Resolve(grpc_resolver_args* args) override { return nullptr; }
};

TEST(PluginResolverTest, FailToInstantiateResolver) {
  register_resolver_factory("zero", new ZeroResolverFactory());

  grpc_core::ExecCtx exec_ctx;
  grpc_combiner* combiner = grpc_combiner_create();
  // Create resolver.
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      build_custom_resolver("zero", combiner);
  ASSERT_EQ(resolver.get(), nullptr);

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
