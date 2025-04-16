// Copyright 2024 The gRPC Authors
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

#ifdef GPR_WINDOWS
#include <grpc/event_engine/event_engine.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/windows/native_windows_dns_resolver.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/host_port.h"
#include "src/core/util/status_helper.h"

namespace grpc_event_engine::experimental {

namespace {
absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
LookupHostnameBlocking(absl::string_view name, absl::string_view default_port) {
  std::vector<EventEngine::ResolvedAddress> addresses;
  // parse name, splitting it into host and port parts
  std::string host;
  std::string port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    return absl::InvalidArgumentError(absl::StrCat("Unparsable name: ", name));
  }
  if (port.empty()) {
    if (default_port.empty()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("No port in name %s or default_port argument", name));
    }
    port = std::string(default_port);
  }
  // Call getaddrinfo
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;      // ipv4 or ipv6
  hints.ai_socktype = SOCK_STREAM;  // stream socket
  hints.ai_flags = AI_PASSIVE;      // for wildcard IP address
  struct addrinfo* result = nullptr;
  int getaddrinfo_error =
      getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  if (getaddrinfo_error != 0) {
    return absl::UnknownError(
        absl::StrFormat("Address lookup failed for %s os_error: %s", name,
                        grpc_core::StatusToString(
                            GRPC_WSA_ERROR(WSAGetLastError(), "getaddrinfo"))
                            .c_str()));
  }
  // Success path: collect and return all addresses
  for (auto* resp = result; resp != nullptr; resp = resp->ai_next) {
    addresses.emplace_back(resp->ai_addr, resp->ai_addrlen);
  }
  if (result) freeaddrinfo(result);
  return addresses;
}

}  // namespace
NativeWindowsDNSResolver::NativeWindowsDNSResolver(
    std::shared_ptr<EventEngine> event_engine)
    : event_engine_(std::move(event_engine)) {}

void NativeWindowsDNSResolver::LookupHostname(
    EventEngine::DNSResolver::LookupHostnameCallback on_resolved,
    absl::string_view name, absl::string_view default_port) {
  event_engine_->Run(
      [name, default_port, on_resolved = std::move(on_resolved)]() mutable {
        on_resolved(LookupHostnameBlocking(name, default_port));
      });
}

void NativeWindowsDNSResolver::LookupSRV(
    EventEngine::DNSResolver::LookupSRVCallback on_resolved,
    absl::string_view /* name */) {
  // Not supported
  event_engine_->Run([on_resolved = std::move(on_resolved)]() mutable {
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up SRV records"));
  });
}

void NativeWindowsDNSResolver::LookupTXT(
    EventEngine::DNSResolver::LookupTXTCallback on_resolved,
    absl::string_view /* name */) {
  // Not supported
  event_engine_->Run([on_resolved = std::move(on_resolved)]() mutable {
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up TXT records"));
  });
}

}  // namespace grpc_event_engine::experimental

#endif  // GPR_WINDOWS
