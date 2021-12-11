/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_WINSOCK_SOCKET

#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolve_address_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"

namespace grpc_core {
namespace {

class NativeDNSRequest : public DNSRequest {
 public:
  NativeDNSRequest(
      absl::string_view name, absl::string_view default_port,
      std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done)
      : name_(name), default_port_(default_port), on_done_(std::move(on_done)) {
    GRPC_CLOSURE_INIT(&request_closure_, DoRequestThread, this, nullptr);
  }

  // Starts the resolution
  void Start() override {
    Ref().release();  // ref held by callback
    Executor::Run(&request_closure_, GRPC_ERROR_NONE, ExecutorType::RESOLVER);
  }

  // This is a no-op for the native resolver. Note
  // that no I/O polling is required for the resolution to finish.
  void Orphan() override { Unref(); }

 private:
  // Callback to be passed to grpc Executor to asynch-ify
  // BlockingResolveAddress
  static void DoRequestThread(void* rp, grpc_error_handle /*error*/) {
    NativeDNSRequest* r = static_cast<NativeDNSRequest*>(rp);
    auto result =
        GetDNSResolver()->BlockingResolveAddress(r->name_, r->default_port_);
    // running inline is safe since we've already been scheduled on the executor
    r->on_done_(result);
    r->Unref();
  }

  const std::string name_;
  const std::string default_port_;
  const std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done_;
  grpc_closure request_closure_;
};

NativeDNSResolver* g_native_dns_resolver;
}  // namespace

NativeDNSResolver* NativeDNSResolver::GetOrCreate() {
  if (g_native_dns_resolver == nullptr) {
    g_native_dns_resolver = new NativeDNSResolver();
  }
  return g_native_dns_resolver;
}

OrphanablePtr<DNSRequest> NativeDNSResolver::CreateRequest(
    absl::string_view name, absl::string_view default_port,
    grpc_pollset_set* /* interested_parties */,
    std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done) {
  return MakeOrphanable<NativeDNSRequest>(name, default_port,
                                          std::move(on_done));
}

absl::StatusOr<grpc_resolved_addresses*>
NativeDNSResolver::BlockingResolveAddress(absl::string_view name,
                                          absl::string_view default_port) {
  ExecCtx exec_ctx;
  struct addrinfo hints;
  struct addrinfo *result = NULL, *resp;
  int s;
  size_t i;
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_resolved_addresses* addresses = nullptr;

  // parse name, splitting it into host and port parts
  std::string host;
  std::string port;
  SplitHostPort(name, &host, &port);
  if (host.empty()) {
    error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrFormat("unparseable host:port: '%s'", name));
    goto done;
  }
  if (port.empty()) {
    if (default_port == NULL) {
      error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrFormat("no port in name '%s'", name));
      goto done;
    }
    port = std::string(default_port);
  }

  // Call getaddrinfo
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "getaddrinfo");
    goto done;
  }

  // Success path: set addrs non-NULL, fill it in
  addresses =
      (grpc_resolved_addresses*)gpr_malloc(sizeof(grpc_resolved_addresses));
  addresses->naddrs = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    addresses->naddrs++;
  }
  addresses->addrs = (grpc_resolved_address*)gpr_malloc(
      sizeof(grpc_resolved_address) * addresses->naddrs);
  i = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    memcpy(&addresses->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    addresses->addrs[i].len = resp->ai_addrlen;
    i++;
  }

done:
  if (result) {
    freeaddrinfo(result);
  }
  if (error == GRPC_ERROR_NONE) {
    return addresses;
  }
  return grpc_error_to_absl_status(error);
}

}  // namespace grpc_core

#endif
