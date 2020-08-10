/*
 *
 * Copyright 2019 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/match.h"
#include "re2/re2.h"

#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {

TraceFlag grpc_xds_resolver_trace(false, "xds_resolver");

const char* kCallAttributeRoutingAction = "routing_action";

namespace {

//
// XdsResolver
//

class XdsResolver : public Resolver {
 public:
  explicit XdsResolver(ResolverArgs args)
      : Resolver(std::move(args.work_serializer),
                 std::move(args.result_handler)),
        args_(grpc_channel_args_copy(args.args)),
        interested_parties_(args.pollset_set) {
    char* path = args.uri->path;
    if (path[0] == '/') ++path;
    server_name_ = path;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] created for server name %s", this,
              server_name_.c_str());
    }
  }

  ~XdsResolver() override {
    grpc_channel_args_destroy(args_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] destroyed", this);
    }
  }

  void StartLocked() override;

  void ShutdownLocked() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] shutting down", this);
    }
    xds_client_.reset();
  }

 private:
  struct ClusterState {
    int refcount = 0;
  };

  class ServiceConfigWatcher : public XdsClient::ServiceConfigWatcherInterface {
   public:
    explicit ServiceConfigWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnServiceConfigChanged(RefCountedPtr<ServiceConfig> service_config,
                                const XdsApi::RdsUpdate& rds_update) override;
    void OnError(grpc_error* error) override;
    void OnResourceDoesNotExist() override;

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class XdsConfigSelector : public ConfigSelector {
   public:
    XdsConfigSelector(RefCountedPtr<XdsResolver> resolver,
                      const XdsApi::RdsUpdate& rds_update)
        : resolver_(std::move(resolver)) {
      for (const auto& update : rds_update.routes) {
        // todo@ donna weighted target name
        // Keep a set of actions from this update and take a reference for every
        // cluster. Ref count will be unset in the destructor.
        actions_.emplace(update.cluster_name);
        {
          MutexLock lock(&resolver_->cluster_state_map_mu_);
          resolver_->cluster_state_map_[update.cluster_name].refcount++;
        }
        /*void *stack[128];
        int size = absl::GetStackTrace(stack, 128, 1);
        for (int i = 0; i < size; ++i) {
          char out[256];
          if (absl::Symbolize(stack[i], out, 256)) {
            gpr_log(GPR_INFO, "donna stack trace per selector add:[%s]", out);
          }
        }*/
        XdsApi::RdsUpdate::RdsRoute route;
        route.matchers.path_matcher.type = update.matchers.path_matcher.type;
        route.matchers.path_matcher.string_matcher =
            update.matchers.path_matcher.string_matcher;
        if (route.matchers.path_matcher.type ==
            XdsApi::RdsUpdate::RdsRoute::Matchers::PathMatcher::
                PathMatcherType::REGEX) {
          route.matchers.path_matcher.regex_matcher = absl::make_unique<RE2>(
              update.matchers.path_matcher.regex_matcher->pattern());
        }
        // todo@ donnadionne header matchers
        route.cluster_name = update.cluster_name;
        // todo@ donnadionne weighted clusters.
        route_table_.routes.emplace_back(std::move(route));
      }
      gpr_log(GPR_INFO,
              "DONNAA: RDS update passed in;  RouteConfiguration contains "
              "%" PRIuPTR
              " routes to build the route_table_ for XdsConfigSelector",
              route_table_.routes.size());
      for (size_t i = 0; i < route_table_.routes.size(); ++i) {
        gpr_log(GPR_INFO, "Route %" PRIuPTR ":\n%s", i,
                route_table_.routes[i].ToString().c_str());
      }
      {
        MutexLock lock(&resolver_->cluster_state_map_mu_);
        for (const auto& state : resolver_->cluster_state_map_) {
          gpr_log(GPR_INFO, "DONNAAA: constructor: cluster %s and count %d",
                  state.first.c_str(), state.second.refcount);
        }
      }
    }

    ~XdsConfigSelector() {
      MutexLock lock(&resolver_->cluster_state_map_mu_);
      for (const auto& action : actions_) {
        resolver_->cluster_state_map_[action].refcount--;
        /*void *stack[128];
        int size = absl::GetStackTrace(stack, 128, 1);
        for (int i = 0; i < size; ++i) {
          char out[256];
          if (absl::Symbolize(stack[i], out, 256)) {
            gpr_log(GPR_INFO, "donna stack trace per selector minus:[%s]", out);
          }
        }*/
        if (resolver_->cluster_state_map_[action].refcount == 0) {
          resolver_->cluster_state_map_.erase(action);
        }
      }
      for (const auto& state : resolver_->cluster_state_map_) {
        gpr_log(GPR_INFO, "DONNAAA: destructor: cluster %s and count %d",
                state.first.c_str(), state.second.refcount);
      }
    }

    void OnCallCommited(const std::string& routing_action) {
      gpr_log(GPR_INFO, "DONNAAA: OnCallCommitted");
      MutexLock lock(&resolver_->cluster_state_map_mu_);
      resolver_->cluster_state_map_[routing_action].refcount--;
      /*void *stack[128];
      int size = absl::GetStackTrace(stack, 128, 1);
      for (int i = 0; i < size; ++i) {
        char out[256];
        if (absl::Symbolize(stack[i], out, 256)) {
          gpr_log(GPR_INFO, "donna stack trace per call minus:[%s]", out);
        }
      }*/
    }

    CallConfig GetCallConfig(GetCallConfigArgs args) override {
      /*gpr_log(GPR_INFO, "DONNAA NEW path is %s",
              GRPC_SLICE_START_PTR(*(args.path)));
      for (grpc_linked_mdelem* md = args.initial_metadata->list.head;
           md != nullptr; md = md->next) {
        char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
        char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
        gpr_log(GPR_INFO, "key[%s]: value[%s]", key, value);
        gpr_free(key);
        gpr_free(value);
      }*/
      for (size_t i = 0; i < route_table_.routes.size(); ++i) {
        // gpr_log(GPR_INFO, "Route %" PRIuPTR ":\n%s", i,
        //        route_table_.routes[i].matchers.ToString().c_str());
        // gpr_log(GPR_INFO, "Route action %s",
        //        route_table_.routes[i].cluster_name.c_str());
        if (absl::StartsWith(
                StringViewFromSlice(*args.path),
                route_table_.routes[i].matchers.path_matcher.string_matcher)) {
          gpr_log(GPR_INFO, "DONNAA match action found: %s",
                  route_table_.routes[i].cluster_name.c_str());
          char* routing_action_str = static_cast<char*>(args.arena->Alloc(
              route_table_.routes[i].cluster_name.size() + 1));
          strcpy(routing_action_str,
                 route_table_.routes[i].cluster_name.c_str());
          CallConfig call_config;
          call_config.call_attributes[kCallAttributeRoutingAction] =
              absl::string_view(routing_action_str);
          call_config.on_call_committed =
              absl::bind_front(&XdsResolver::XdsConfigSelector::OnCallCommited,
                               this, route_table_.routes[i].cluster_name);
          {
            MutexLock lock(&resolver_->cluster_state_map_mu_);
            resolver_->cluster_state_map_[route_table_.routes[i].cluster_name]
                .refcount++;
          }
          /*void *stack[128];
          int size = absl::GetStackTrace(stack, 128, 1);
          for (int i = 0; i < size; ++i) {
            char out[256];
            if (absl::Symbolize(stack[i], out, 256)) {
              gpr_log(GPR_INFO, "donna stack trace per call add:[%s]", out);
            }
          }*/
          return call_config;
        }
      }
      return CallConfig();
    }

   private:
    RefCountedPtr<XdsResolver> resolver_;
    XdsApi::RdsUpdate route_table_;
    std::set<std::string> actions_;
  };

  std::string server_name_;
  const grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;
  std::map<std::string /* cluster_name */, ClusterState> cluster_state_map_;
  Mutex cluster_state_map_mu_;  // protects the cluster state map.
};

void XdsResolver::ServiceConfigWatcher::OnServiceConfigChanged(
    RefCountedPtr<ServiceConfig> service_config,
    const XdsApi::RdsUpdate& rds_update) {
  if (resolver_->xds_client_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated service config: %s",
            resolver_.get(), service_config->json_string().c_str());
  }
  RefCountedPtr<XdsConfigSelector> config_selector =
      MakeRefCounted<XdsConfigSelector>(resolver_, rds_update);
  grpc_arg new_args[] = {
      resolver_->xds_client_->MakeChannelArg(),
      config_selector->MakeChannelArg(),
  };
  Result result;
  result.args = grpc_channel_args_copy_and_add(resolver_->args_, new_args,
                                               GPR_ARRAY_SIZE(new_args));
  result.service_config = std::move(service_config);
  resolver_->result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::ServiceConfigWatcher::OnError(grpc_error* error) {
  if (resolver_->xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR, "[xds_resolver %p] received error: %s", resolver_.get(),
          grpc_error_string(error));
  grpc_arg xds_client_arg = resolver_->xds_client_->MakeChannelArg();
  Result result;
  result.args =
      grpc_channel_args_copy_and_add(resolver_->args_, &xds_client_arg, 1);
  result.service_config_error = error;
  resolver_->result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::ServiceConfigWatcher::OnResourceDoesNotExist() {
  if (resolver_->xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR,
          "[xds_resolver %p] LDS/RDS resource does not exist -- returning "
          "empty service config",
          resolver_.get());
  Result result;
  result.service_config =
      ServiceConfig::Create("{}", &result.service_config_error);
  GPR_ASSERT(result.service_config != nullptr);
  result.args = grpc_channel_args_copy(resolver_->args_);
  resolver_->result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::StartLocked() {
  grpc_error* error = GRPC_ERROR_NONE;
  xds_client_ = MakeOrphanable<XdsClient>(
      work_serializer(), interested_parties_, server_name_,
      absl::make_unique<ServiceConfigWatcher>(Ref()), *args_, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "Failed to create xds client -- channel will remain in "
            "TRANSIENT_FAILURE: %s",
            grpc_error_string(error));
    result_handler()->ReturnError(error);
  }
}

//
// Factory
//

class XdsResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const grpc_uri* uri) const override {
    if (GPR_UNLIKELY(0 != strcmp(uri->authority, ""))) {
      gpr_log(GPR_ERROR, "URI authority not supported");
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<XdsResolver>(std::move(args));
  }

  const char* scheme() const override { return "xds"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_xds_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::XdsResolverFactory>());
}

void grpc_resolver_xds_shutdown() {}
