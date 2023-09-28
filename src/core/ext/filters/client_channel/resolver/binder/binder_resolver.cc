// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <algorithm>

#include "absl/status/status.h"

#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/port.h"  // IWYU pragma: keep

#ifdef GRPC_HAVE_UNIX_SOCKET

#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace {

class BinderResolver : public Resolver {
 public:
  BinderResolver(ServerAddressList addresses, ResolverArgs args)
      : result_handler_(std::move(args.result_handler)),
        addresses_(std::move(addresses)),
        channel_args_(std::move(args.args)) {}

  void StartLocked() override {
    Result result;
    result.addresses = std::move(addresses_);
    result.args = channel_args_;
    channel_args_ = ChannelArgs();
    result_handler_->ReportResult(std::move(result));
  }

  void ShutdownLocked() override {}

 private:
  std::unique_ptr<ResultHandler> result_handler_;
  ServerAddressList addresses_;
  ChannelArgs channel_args_;
};

class BinderResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "binder"; }

  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    ServerAddressList addresses;
    if (!ParseUri(args.uri, &addresses)) return nullptr;
    return MakeOrphanable<BinderResolver>(std::move(addresses),
                                          std::move(args));
  }

 private:
  static grpc_error_handle BinderAddrPopulate(
      absl::string_view path, grpc_resolved_address* resolved_addr) {
    path = absl::StripPrefix(path, "/");
    if (path.empty()) {
      return GRPC_ERROR_CREATE("path is empty");
    }
    // Store parsed path in a unix socket so it can be reinterpreted as
    // sockaddr. An invalid address family (AF_MAX) is set to make sure it won't
    // be accidentally used.
    memset(resolved_addr, 0, sizeof(*resolved_addr));
    struct sockaddr_un* un =
        reinterpret_cast<struct sockaddr_un*>(resolved_addr->addr);
    un->sun_family = AF_MAX;
    static_assert(sizeof(un->sun_path) >= 101,
                  "unix socket path size is unexpectedly short");
    if (path.size() + 1 > sizeof(un->sun_path)) {
      return GRPC_ERROR_CREATE(
          absl::StrCat(path, " is too long to be handled"));
    }
    // `un` has already be set to zero, no need to append null after the string
    memcpy(un->sun_path, path.data(), path.size());
    resolved_addr->len =
        static_cast<socklen_t>(sizeof(un->sun_family) + path.size() + 1);
    return absl::OkStatus();
  }

  static bool ParseUri(const URI& uri, ServerAddressList* addresses) {
    grpc_resolved_address addr;
    {
      if (!uri.authority().empty()) {
        gpr_log(GPR_ERROR, "authority is not supported in binder scheme");
        return false;
      }
      grpc_error_handle error = BinderAddrPopulate(uri.path(), &addr);
      if (!error.ok()) {
        gpr_log(GPR_ERROR, "%s", StatusToString(error).c_str());
        return false;
      }
    }
    if (addresses != nullptr) {
      addresses->emplace_back(addr, ChannelArgs());
    }
    return true;
  }
};

}  // namespace

void RegisterBinderResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<BinderResolverFactory>());
}

}  // namespace grpc_core

#endif
