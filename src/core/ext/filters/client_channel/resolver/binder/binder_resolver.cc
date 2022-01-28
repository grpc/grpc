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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

#include <sys/un.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"

namespace grpc_core {
namespace {

class BinderResolver : public Resolver {
 public:
  BinderResolver(ServerAddressList addresses, ResolverArgs args)
      : result_handler_(std::move(args.result_handler)),
        addresses_(std::move(addresses)),
        channel_args_(grpc_channel_args_copy(args.args)) {}

  ~BinderResolver() override { grpc_channel_args_destroy(channel_args_); };

  void StartLocked() override {
    Result result;
    result.addresses = std::move(addresses_);
    result.args = channel_args_;
    channel_args_ = nullptr;
    result_handler_->ReportResult(std::move(result));
  }

  void ShutdownLocked() override {}

 private:
  std::unique_ptr<ResultHandler> result_handler_;
  ServerAddressList addresses_;
  const grpc_channel_args* channel_args_ = nullptr;
};

class BinderResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    return ParseUri(uri, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    ServerAddressList addresses;
    if (!ParseUri(args.uri, &addresses)) return nullptr;
    return MakeOrphanable<BinderResolver>(std::move(addresses),
                                          std::move(args));
  }

  const char* scheme() const override { return "binder"; }

 private:
  static grpc_error_handle BinderAddrPopulate(
      absl::string_view path, grpc_resolved_address* resolved_addr) {
    path = absl::StripPrefix(path, "/");
    if (path.empty()) {
      return GRPC_ERROR_CREATE_FROM_CPP_STRING("path is empty");
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
      return GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrCat(path, " is too long to be handled"));
    }
    // `un` has already be set to zero, no need to append null after the string
    memcpy(un->sun_path, path.data(), path.size());
    resolved_addr->len =
        static_cast<socklen_t>(sizeof(un->sun_family) + path.size() + 1);
    return GRPC_ERROR_NONE;
  }

  static bool ParseUri(const URI& uri, ServerAddressList* addresses) {
    grpc_resolved_address addr;
    {
      if (!uri.authority().empty()) {
        gpr_log(GPR_ERROR, "authority is not supported in binder scheme");
        return false;
      }
      grpc_error_handle error = BinderAddrPopulate(uri.path(), &addr);
      if (error != GRPC_ERROR_NONE) {
        gpr_log(GPR_ERROR, "%s", grpc_error_std_string(error).c_str());
        GRPC_ERROR_UNREF(error);
        return false;
      }
    }
    if (addresses != nullptr) {
      addresses->emplace_back(addr, nullptr /* args */);
    }
    return true;
  }
};

}  // namespace
}  // namespace grpc_core

void grpc_resolver_binder_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::BinderResolverFactory>());
}

void grpc_resolver_binder_shutdown() {}

#else

void grpc_resolver_binder_init() {}

void grpc_resolver_binder_shutdown() {}

#endif
