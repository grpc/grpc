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

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_WINSOCK_SOCKET

#include <grpc/support/alloc.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <string>

#include "absl/strings/str_format.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolve_address_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/crash.h"
#include "src/core/util/host_port.h"
#include "src/core/util/string.h"
#include "src/core/util/thd.h"

namespace grpc_core {
namespace {

class NativeDNSRequest {
 public:
  NativeDNSRequest(
      absl::string_view name, absl::string_view default_port,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done)
      : name_(name), default_port_(default_port), on_done_(std::move(on_done)) {
    GRPC_CLOSURE_INIT(&request_closure_, DoRequestThread, this, nullptr);
    Executor::Run(&request_closure_, absl::OkStatus(), ExecutorType::RESOLVER);
  }

 private:
  // Callback to be passed to grpc Executor to asynch-ify
  // LookupHostnameBlocking
  static void DoRequestThread(void* rp, grpc_error_handle /*error*/) {
    NativeDNSRequest* r = static_cast<NativeDNSRequest*>(rp);
    auto result =
        GetDNSResolver()->LookupHostnameBlocking(r->name_, r->default_port_);
    // running inline is safe since we've already been scheduled on the executor
    r->on_done_(std::move(result));
    delete r;
  }

  const std::string name_;
  const std::string default_port_;
  const std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
      on_done_;
  grpc_closure request_closure_;
};

}  // namespace

NativeDNSResolver::NativeDNSResolver() {}

DNSResolver::TaskHandle NativeDNSResolver::LookupHostname(
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolved,
    absl::string_view name, absl::string_view default_port,
    Duration /* timeout */, grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  new NativeDNSRequest(name, default_port, std::move(on_resolved));
  return kNullHandle;
}

absl::StatusOr<std::vector<grpc_resolved_address>>
NativeDNSResolver::LookupHostnameBlocking(absl::string_view name,
                                          absl::string_view default_port) {
  ExecCtx exec_ctx;
  struct addrinfo hints;
  struct addrinfo *result = NULL, *resp;
  int s;
  grpc_error_handle error;
  std::vector<grpc_resolved_address> addresses;

  // parse name, splitting it into host and port parts
  std::string host;
  std::string port;
  SplitHostPort(name, &host, &port);
  if (host.empty()) {
    error =
        GRPC_ERROR_CREATE(absl::StrFormat("unparsable host:port: '%s'", name));
    goto done;
  }
  if (port.empty()) {
    if (default_port.empty()) {
      error = GRPC_ERROR_CREATE(absl::StrFormat("no port in name '%s'", name));
      goto done;
    }
    port = std::string(default_port);
  }

  // Call getaddrinfo
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;      // ipv4 or ipv6
  hints.ai_socktype = SOCK_STREAM;  // stream socket
  hints.ai_flags = AI_PASSIVE;      // for wildcard IP address

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "getaddrinfo");
    goto done;
  }

  // Success path: set addrs non-NULL, fill it in
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    grpc_resolved_address addr;
    memcpy(&addr.addr, resp->ai_addr, resp->ai_addrlen);
    addr.len = resp->ai_addrlen;
    addresses.push_back(addr);
  }

done:
  if (result) {
    freeaddrinfo(result);
  }
  if (error.ok()) {
    return addresses;
  }
  auto error_result = grpc_error_to_absl_status(error);
  return error_result;
}

void RunCallbackOnDefaultEventEngine(absl::AnyInvocable<void()> f) {
  auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  engine->Run([f = std::move(f), engine]() mutable { f(); });
}

DNSResolver::TaskHandle NativeDNSResolver::LookupSRV(
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolved,
    absl::string_view /* name */, Duration /* deadline */,
    grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  RunCallbackOnDefaultEventEngine([on_resolved] {
    ApplicationCallbackExecCtx app_exec_ctx;
    ExecCtx exec_ctx;
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up SRV records"));
  });
  return {-1, -1};
};

DNSResolver::TaskHandle NativeDNSResolver::LookupTXT(
    std::function<void(absl::StatusOr<std::string>)> on_resolved,
    absl::string_view /* name */, Duration /* timeout */,
    grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  // Not supported
  RunCallbackOnDefaultEventEngine([on_resolved] {
    ApplicationCallbackExecCtx app_exec_ctx;
    ExecCtx exec_ctx;
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up TXT records"));
  });
  return {-1, -1};
};

bool NativeDNSResolver::Cancel(TaskHandle /*handle*/) { return false; }

}  // namespace grpc_core

#endif
