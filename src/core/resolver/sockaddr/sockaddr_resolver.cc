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

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/uri.h"

namespace grpc_core {

namespace {

class SockaddrResolver final : public Resolver {
 public:
  SockaddrResolver(EndpointAddressesList addresses, ResolverArgs args);

  void StartLocked() override;

  void ShutdownLocked() override {}

 private:
  std::unique_ptr<ResultHandler> result_handler_;
  EndpointAddressesList addresses_;
  ChannelArgs channel_args_;
};

SockaddrResolver::SockaddrResolver(EndpointAddressesList addresses,
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
              EndpointAddressesList* addresses) {
  if (!uri.authority().empty()) {
    LOG(ERROR) << "authority-based URIs not supported by the " << uri.scheme()
               << " scheme";
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
  EndpointAddressesList addresses;
  if (!ParseUri(args.uri, parse, &addresses)) return nullptr;
  // Instantiate resolver.
  return MakeOrphanable<SockaddrResolver>(std::move(addresses),
                                          std::move(args));
}

class IPv4ResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "ipv4"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_ipv4, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_ipv4);
  }
};

class IPv6ResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "ipv6"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_ipv6, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_ipv6);
  }
};

#ifdef GRPC_HAVE_UNIX_SOCKET
class UnixResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "unix"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_unix, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_unix);
  }
};

class UnixAbstractResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "unix-abstract"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_unix_abstract, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_unix_abstract);
  }
};
#endif  // GRPC_HAVE_UNIX_SOCKET

#ifdef GRPC_HAVE_VSOCK
class VSockResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "vsock"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, grpc_parse_vsock, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_vsock);
  }
};

#endif  // GRPC_HAVE_VSOCK

}  // namespace

void RegisterSockaddrResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<IPv4ResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<IPv6ResolverFactory>());
#ifdef GRPC_HAVE_UNIX_SOCKET
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<UnixResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<UnixAbstractResolverFactory>());
#endif
#ifdef GRPC_HAVE_VSOCK
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<VSockResolverFactory>());
#endif
}

}  // namespace grpc_core
