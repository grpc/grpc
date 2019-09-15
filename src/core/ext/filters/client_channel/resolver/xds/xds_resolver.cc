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

namespace grpc_core {

namespace {

class XdsResolver : public Resolver {
 public:
  explicit XdsResolver(ResolverArgs args)
      : Resolver(args.combiner, std::move(args.result_handler)),
        args_(grpc_channel_args_copy(args.args)) {}
  ~XdsResolver() override { grpc_channel_args_destroy(args_); }

  void StartLocked() override;

  void ShutdownLocked() override{};

 private:
  const grpc_channel_args* args_;
};

void XdsResolver::StartLocked() {
  static const char* service_config =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"xds_experimental\":{} }\n"
      "  ]\n"
      "}";
  Result result;
  result.args = args_;
  args_ = nullptr;
  grpc_error* error = GRPC_ERROR_NONE;
  result.service_config = ServiceConfig::Create(service_config, &error);
  result_handler()->ReturnResult(std::move(result));
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
    return OrphanablePtr<Resolver>(New<XdsResolver>(std::move(args)));
  }

  const char* scheme() const override { return "xds-experimental"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_xds_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::XdsResolverFactory>()));
}

void grpc_resolver_xds_shutdown() {}
