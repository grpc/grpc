//
// Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/resolve_address_custom.h"

#include <string.h>

#include <cstdio>
#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolve_address_impl.h"
#include "src/core/lib/transport/error_utils.h"

void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addresses) {
  if (addresses != nullptr) {
    gpr_free(addresses->addrs);
  }
  gpr_free(addresses);
}

void grpc_custom_resolve_callback(grpc_custom_resolver* resolver,
                                  grpc_resolved_addresses* result,
                                  grpc_error_handle error) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::CustomDNSResolver::Request* request =
      grpc_core::CustomDNSResolver::Request::FromC(resolver);
  if (error != GRPC_ERROR_NONE) {
    request->ResolveCallback(grpc_error_to_absl_status(error));
  } else {
    std::vector<grpc_resolved_address> addresses;
    for (size_t i = 0; i < result->naddrs; i++) {
      addresses.push_back(result->addrs[i]);
    }
    request->ResolveCallback(std::move(addresses));
    grpc_resolved_addresses_destroy(result);
  }
  GRPC_ERROR_UNREF(error);
}

namespace grpc_core {

namespace {

absl::Status TrySplitHostPort(absl::string_view name,
                              absl::string_view default_port, std::string* host,
                              std::string* port) {
  // parse name, splitting it into host and port parts
  SplitHostPort(name, host, port);
  if (host->empty()) {
    return absl::UnknownError(
        absl::StrFormat("unparseable host:port: '%s'", name));
  }
  if (port->empty()) {
    // TODO(murgatroid99): add tests for this case
    if (default_port == nullptr) {
      return absl::UnknownError(absl::StrFormat("no port in name '%s'", name));
    }
    *port = std::string(default_port);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> NamedPortToNumeric(absl::string_view named_port) {
  if (named_port == "http") {
    return "80";
  } else if (named_port == "https") {
    return "443";
  } else {
    return absl::UnknownError(absl::StrCat("unknown named port: ", named_port));
  }
}

}  // namespace

void CustomDNSResolver::Request::ResolveCallback(
    absl::StatusOr<std::vector<grpc_resolved_address>> result) {
  if (!result.ok()) {
    auto numeric_port_or = NamedPortToNumeric(port_);
    if (numeric_port_or.ok()) {
      port_ = *numeric_port_or;
      resolve_address_vtable_->resolve_async(c_ptr(), host_.c_str(),
                                             port_.c_str());
      // keep holding ref for active resolution
      return;
    }
  }
  // since we can't guarantee that we're not being called inline from
  // Start(), run the callback on the ExecCtx.
  new DNSCallbackExecCtxScheduler(std::move(on_done_), std::move(result));
  Unref();
}

namespace {
CustomDNSResolver* g_custom_dns_resolver;
}  // namespace

// Creates the global custom resolver with the specified vtable.
void CustomDNSResolver::Create(grpc_custom_resolver_vtable* vtable) {
  if (g_custom_dns_resolver != nullptr) return;
  g_custom_dns_resolver = new CustomDNSResolver(vtable);
}

// Gets the singleton instance.
CustomDNSResolver* CustomDNSResolver::Get() { return g_custom_dns_resolver; }

absl::StatusOr<std::vector<grpc_resolved_address>>
CustomDNSResolver::ResolveNameBlocking(absl::string_view name,
                                       absl::string_view default_port) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();

  std::string host;
  std::string port;
  absl::Status parse_status =
      TrySplitHostPort(name, default_port, &host, &port);
  if (!parse_status.ok()) {
    return parse_status;
  }

  // Call getaddrinfo
  ExecCtx* curr = ExecCtx::Get();
  ExecCtx::Set(nullptr);
  grpc_resolved_addresses* addrs;
  grpc_error_handle err =
      resolve_address_vtable_->resolve(host.c_str(), port.c_str(), &addrs);
  if (err != GRPC_ERROR_NONE) {
    auto numeric_port_or = NamedPortToNumeric(port);
    if (numeric_port_or.ok()) {
      port = *numeric_port_or;
      GRPC_ERROR_UNREF(err);
      err =
          resolve_address_vtable_->resolve(host.c_str(), port.c_str(), &addrs);
    }
  }
  ExecCtx::Set(curr);
  if (err == GRPC_ERROR_NONE) {
    GPR_ASSERT(addrs != nullptr);
    std::vector<grpc_resolved_address> result;
    for (size_t i = 0; i < addrs->naddrs; i++) {
      result.push_back(addrs->addrs[i]);
    }
    grpc_resolved_addresses_destroy(addrs);
    return result;
  }
  auto error_result = grpc_error_to_absl_status(err);
  GRPC_ERROR_UNREF(err);
  return error_result;
}

void CustomDNSResolver::Request::Start() {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  absl::Status parse_status =
      TrySplitHostPort(name_, default_port_, &host_, &port_);
  if (!parse_status.ok()) {
    new DNSCallbackExecCtxScheduler(std::move(on_done_),
                                    std::move(parse_status));
    return;
  }
  // Call getaddrinfo
  Ref().release();  // ref held by resolution
  resolve_address_vtable_->resolve_async(c_ptr(), host_.c_str(), port_.c_str());
}

}  // namespace grpc_core
