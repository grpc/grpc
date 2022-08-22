//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/resolver/address_parser_registry.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

namespace {

class SockaddrResolver final : public Resolver {
 public:
  SockaddrResolver(ServerAddressList addresses, ResolverArgs args);

  void StartLocked() override;

  void ShutdownLocked() override {}

 private:
  std::unique_ptr<ResultHandler> result_handler_;
  ServerAddressList addresses_;
  ChannelArgs channel_args_;
};

SockaddrResolver::SockaddrResolver(ServerAddressList addresses,
                                   ResolverArgs args)
    : result_handler_(std::move(args.result_handler)),
      addresses_(std::move(addresses)),
      channel_args_(std::move(args.args)) {}

void SockaddrResolver::StartLocked() {
  Result result;
  result.addresses = std::move(addresses_);
  result.args = std::move(channel_args_);
  result_handler_->ReportResult(std::move(result));
}

//
// Factory
//

class SockaddrResolverFactory final : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    return CoreConfiguration::Get().address_parser_registry().Parse(uri).ok();
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    auto addresses =
        CoreConfiguration::Get().address_parser_registry().Parse(args.uri);
    if (!addresses.ok()) return nullptr;
    ServerAddressList server_addresses;
    server_addresses.reserve(addresses->size());
    for (auto address : *addresses) {
      server_addresses.emplace_back(address, ChannelArgs());
    }
    return MakeOrphanable<SockaddrResolver>(std::move(server_addresses),
                                            std::move(args));
  }

  bool ImplementsScheme(absl::string_view scheme) const override {
    return CoreConfiguration::Get().address_parser_registry().HasScheme(scheme);
  }
};

}  // namespace

void RegisterSockaddrResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      absl::make_unique<SockaddrResolverFactory>());
}

}  // namespace grpc_core
