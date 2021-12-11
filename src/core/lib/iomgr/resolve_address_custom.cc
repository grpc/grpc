/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/resolve_address_custom.h"

#include <string.h>

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
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace {

absl::Status TrySplitHostPort(absl::string_view name,
                              absl::string_view default_port, std::string* host,
                              std::string* port) {
  /* parse name, splitting it into host and port parts */
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

void CustomDNSRequest::ResolveCallback(grpc_resolved_addresses* result,
                                       grpc_error_handle error) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  ApplicationCallbackExecCtx callback_exec_ctx;
  ExecCtx exec_ctx;
  OrphanablePtr<CustomDNSRequest> unreffer(this);
  if (error == GRPC_ERROR_NONE) {
    // since we can't guarantee that we're not being called inline from
    // Start(), run the callback on the ExecCtx.
    new DNSCallbackExecCtxScheduler(std::move(on_done_), result);
    return;
  } else {
    auto numeric_port_or = NamedPortToNumeric(port_);
    if (numeric_port_or.ok()) {
      port_ = *numeric_port_or;
      auto* self =
          unreffer.release();  // keep holding ref for active resolution
      resolve_address_vtable_->resolve_async(this, host_.c_str(),
                                             port_.c_str());
      return;
    }
  }
  new DNSCallbackExecCtxScheduler(std::move(on_done_),
                                  grpc_error_to_absl_status(error));
}

namespace {
CustomDNSResolver* g_custom_dns_resolver;
}  // namespace

CustomDNSResolver* CustomDNSResolver::GetOrCreate(
    grpc_custom_resolver_vtable* resolve_address_vtable) {
  if (g_custom_dns_resolver == nullptr) {
    g_custom_dns_resolver = new CustomDNSResolver(resolve_address_vtable);
  }
  return g_custom_dns_resolver;
}

absl::StatusOr<grpc_resolved_addresses*>
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

  /* Call getaddrinfo */
  grpc_resolved_addresses* addrs = nullptr;
  ExecCtx* curr = ExecCtx::Get();
  ExecCtx::Set(nullptr);
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
    return addrs;
  }
  return grpc_error_to_absl_status(err);
}

void CustomDNSRequest::Start() {
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
  resolve_address_vtable_->resolve_async(this, host_.c_str(), port_.c_str());
}

}  // namespace grpc_core
