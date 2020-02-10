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

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/lib/gprpp/string_view.h"

namespace grpc_core {

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
    server_name_.reset(gpr_strdup(path));
  }

  ~XdsResolver() override { grpc_channel_args_destroy(args_); }

  void StartLocked() override;

  void ShutdownLocked() override { xds_client_.reset(); }

 private:
  class ServiceConfigWatcher : public XdsClient::ServiceConfigWatcherInterface {
   public:
    explicit ServiceConfigWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnServiceConfigChanged(
        RefCountedPtr<ServiceConfig> service_config) override;
    void OnError(grpc_error* error) override;

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  grpc_core::UniquePtr<char> server_name_;
  const grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;
};

void XdsResolver::ServiceConfigWatcher::OnServiceConfigChanged(
    RefCountedPtr<ServiceConfig> service_config) {
  if (resolver_->xds_client_ == nullptr) return;
  grpc_arg xds_client_arg = resolver_->xds_client_->MakeChannelArg();
  Result result;
  result.args =
      grpc_channel_args_copy_and_add(resolver_->args_, &xds_client_arg, 1);
  result.service_config = std::move(service_config);
  resolver_->result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::ServiceConfigWatcher::OnError(grpc_error* error) {
  if (resolver_->xds_client_ == nullptr) return;
  grpc_arg xds_client_arg = resolver_->xds_client_->MakeChannelArg();
  Result result;
  result.args =
      grpc_channel_args_copy_and_add(resolver_->args_, &xds_client_arg, 1);
  result.service_config_error = error;
  resolver_->result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::StartLocked() {
  grpc_error* error = GRPC_ERROR_NONE;
  xds_client_ = MakeOrphanable<XdsClient>(
      work_serializer(), interested_parties_, StringView(server_name_.get()),
      grpc_core::MakeUnique<ServiceConfigWatcher>(Ref()), *args_, &error);
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

  const char* scheme() const override { return "xds-experimental"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_xds_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::MakeUnique<grpc_core::XdsResolverFactory>());
}

void grpc_resolver_xds_shutdown() {}
