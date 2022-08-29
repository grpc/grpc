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
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

namespace {

class SockaddrResolver : public Resolver {
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

bool ParseUri(const URI& uri,
              bool parse(const URI& uri, grpc_resolved_address* dst),
              ServerAddressList* addresses) {
  if (!uri.authority().empty()) {
    gpr_log(GPR_ERROR, "authority-based URIs not supported by the %s scheme",
            uri.scheme().c_str());
    return false;
  }
  // Construct addresses.
  bool errors_found = false;
  for (absl::string_view ith_path : absl::StrSplit(uri.path(), ',')) {
    if (ith_path.empty()) {
      // Skip targets which are empty.
      continue;
    }
    auto ith_uri = URI::Create(uri.scheme(), "", std::string(ith_path), {}, "");
    grpc_resolved_address addr;
    if (!ith_uri.ok() || !parse(*ith_uri, &addr)) {
      errors_found = true;
      break;
    }
    if (addresses != nullptr) {
      addresses->emplace_back(addr, ChannelArgs());
    }
  }
  return !errors_found;
}

OrphanablePtr<Resolver> CreateSockaddrResolver(
    ResolverArgs args, bool parse(const URI& uri, grpc_resolved_address* dst)) {
  ServerAddressList addresses;
  if (!ParseUri(args.uri, parse, &addresses)) return nullptr;
  // Instantiate resolver.
  return MakeOrphanable<SockaddrResolver>(std::move(addresses),
                                          std::move(args));
}

class IPv4ResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_ipv4, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_ipv4);
  }

  absl::string_view scheme() const override { return "ipv4"; }
};

class IPv6ResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_ipv6, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_ipv6);
  }

  absl::string_view scheme() const override { return "ipv6"; }
};

#ifdef GRPC_HAVE_UNIX_SOCKET
class UnixResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_unix, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_unix);
  }

  std::string GetDefaultAuthority(const URI& /*uri*/) const override {
    return "localhost";
  }

  absl::string_view scheme() const override { return "unix"; }
};

class UnixAbstractResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "unix-abstract"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_unix_abstract, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_unix_abstract);
  }

  std::string GetDefaultAuthority(const URI& /*uri*/) const override {
    return "localhost";
  }
};
#endif  // GRPC_HAVE_UNIX_SOCKET

}  // namespace

void RegisterSockaddrResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      absl::make_unique<IPv4ResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      absl::make_unique<IPv6ResolverFactory>());
#ifdef GRPC_HAVE_UNIX_SOCKET
  builder->resolver_registry()->RegisterResolverFactory(
      absl::make_unique<UnixResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      absl::make_unique<UnixAbstractResolverFactory>());
#endif
}

}  // namespace grpc_core
