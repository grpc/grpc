// Copyright 2023 The gRPC Authors
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

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/cf_engine/dns_service_resolver.h"
#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/host_port.h"

namespace grpc_event_engine {
namespace experimental {

void DNSServiceResolverImpl::LookupHostname(
    EventEngine::DNSResolver::LookupHostnameCallback on_resolve,
    absl::string_view name, absl::string_view default_port) {
  GRPC_TRACE_LOG(event_engine_dns, INFO)
      << "DNSServiceResolverImpl::LookupHostname: name: " << name
      << ", default_port: " << default_port << ", this: " << this;

  absl::string_view host;
  absl::string_view port_string;
  if (!grpc_core::SplitHostPort(name, &host, &port_string)) {
    engine_->Run([on_resolve = std::move(on_resolve),
                  status = absl::InvalidArgumentError(
                      absl::StrCat("Unparseable name: ", name))]() mutable {
      on_resolve(status);
    });
    return;
  }
  if (host.empty()) {
    engine_->Run([on_resolve = std::move(on_resolve),
                  status = absl::InvalidArgumentError(absl::StrCat(
                      "host must not be empty in name: ", name))]() mutable {
      on_resolve(status);
    });
    return;
  }
  if (port_string.empty()) {
    if (default_port.empty()) {
      engine_->Run([on_resolve = std::move(on_resolve),
                    status = absl::InvalidArgumentError(absl::StrFormat(
                        "No port in name %s or default_port argument",
                        name))]() mutable { on_resolve(std::move(status)); });
      return;
    }
    port_string = default_port;
  }

  int port = 0;
  if (port_string == "http") {
    port = 80;
  } else if (port_string == "https") {
    port = 443;
  } else if (!absl::SimpleAtoi(port_string, &port)) {
    engine_->Run([on_resolve = std::move(on_resolve),
                  status = absl::InvalidArgumentError(absl::StrCat(
                      "Failed to parse port in name: ", name))]() mutable {
      on_resolve(std::move(status));
    });
    return;
  }

  // TODO(yijiem): Change this when refactoring code in
  // src/core/lib/address_utils to use EventEngine::ResolvedAddress.
  grpc_resolved_address addr;
  const std::string hostport = grpc_core::JoinHostPort(host, port);
  if (grpc_parse_ipv4_hostport(hostport.c_str(), &addr,
                               /*log_errors=*/false) ||
      grpc_parse_ipv6_hostport(hostport.c_str(), &addr,
                               /*log_errors=*/false)) {
    // Early out if the target is an ipv4 or ipv6 literal, otherwise dns service
    // responses with kDNSServiceErr_NoSuchRecord
    std::vector<EventEngine::ResolvedAddress> result;
    result.emplace_back(reinterpret_cast<sockaddr*>(addr.addr), addr.len);
    engine_->Run([on_resolve = std::move(on_resolve),
                  result = std::move(result)]() mutable {
      on_resolve(std::move(result));
    });
    return;
  }

  DNSServiceRef sdRef;
  auto host_string = std::string{host};
  auto error = DNSServiceGetAddrInfo(
      &sdRef, kDNSServiceFlagsTimeout | kDNSServiceFlagsReturnIntermediates, 0,
      kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, host_string.c_str(),
      &DNSServiceResolverImpl::ResolveCallback, this /* do not Ref */);

  if (error != kDNSServiceErr_NoError) {
    engine_->Run([on_resolve = std::move(on_resolve),
                  status = absl::UnknownError(absl::StrFormat(
                      "DNSServiceGetAddrInfo failed with error:%d",
                      error))]() mutable { on_resolve(std::move(status)); });
    return;
  }

  grpc_core::ReleasableMutexLock lock(&request_mu_);

  error = DNSServiceSetDispatchQueue(sdRef, queue_);
  if (error != kDNSServiceErr_NoError) {
    engine_->Run([on_resolve = std::move(on_resolve),
                  status = absl::UnknownError(absl::StrFormat(
                      "DNSServiceSetDispatchQueue failed with error:%d",
                      error))]() mutable { on_resolve(std::move(status)); });
    return;
  }

  requests_.try_emplace(
      sdRef, DNSServiceRequest{
                 std::move(on_resolve), static_cast<uint16_t>(port), {}});
}

/* static */
void DNSServiceResolverImpl::ResolveCallback(
    DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    DNSServiceErrorType errorCode, const char* hostname,
    const struct sockaddr* address, uint32_t ttl, void* context) {
  GRPC_TRACE_LOG(event_engine_dns, INFO)
      << "DNSServiceResolverImpl::ResolveCallback: sdRef: " << sdRef
      << ", flags: " << flags << ", interface: " << interfaceIndex
      << ", errorCode: " << errorCode << ", hostname: " << hostname
      << ", addressFamily: " << address->sa_family << ", ttl: " << ttl
      << ", this: " << context;

  // no need to increase refcount here, since ResolveCallback and Shutdown is
  // called from the serial queue and it is guarenteed that it won't be called
  // after the sdRef is deallocated
  auto that = static_cast<DNSServiceResolverImpl*>(context);

  grpc_core::ReleasableMutexLock lock(&that->request_mu_);
  auto request_it = that->requests_.find(sdRef);
  CHECK(request_it != that->requests_.end());

  if (errorCode != kDNSServiceErr_NoError &&
      errorCode != kDNSServiceErr_NoSuchRecord) {
    // extrace request and release lock before calling on_resolve
    auto request_node = that->requests_.extract(request_it);
    lock.Release();

    auto& request = request_node.mapped();
    request.on_resolve(absl::UnknownError(absl::StrFormat(
        "address lookup failed for %s: errorCode: %d", hostname, errorCode)));
    DNSServiceRefDeallocate(sdRef);
    return;
  }

  auto& request = request_it->second;

  // set received ipv4 or ipv6 response, even for kDNSServiceErr_NoSuchRecord to
  // mark that the response for the stack is received, it is possible that the
  // one stack receives some results and the other stack gets
  // kDNSServiceErr_NoSuchRecord error.
  if (address->sa_family == AF_INET) {
    request.has_ipv4_response = true;
  } else if (address->sa_family == AF_INET6) {
    request.has_ipv6_response = true;
  }

  // collect results if there is no error (not kDNSServiceErr_NoSuchRecord)
  if (errorCode == kDNSServiceErr_NoError) {
    request.result.emplace_back(address, address->sa_len);
    auto& resolved_address = request.result.back();
    if (address->sa_family == AF_INET) {
      (const_cast<sockaddr_in*>(
           reinterpret_cast<const sockaddr_in*>(resolved_address.address())))
          ->sin_port = htons(request.port);
    } else if (address->sa_family == AF_INET6) {
      (const_cast<sockaddr_in6*>(
           reinterpret_cast<const sockaddr_in6*>(resolved_address.address())))
          ->sin6_port = htons(request.port);
    }

    GRPC_TRACE_LOG(event_engine_dns, INFO)
        << "DNSServiceResolverImpl::ResolveCallback: sdRef: " << sdRef
        << ", hostname: " << hostname << ", addressPort: "
        << ResolvedAddressToString(resolved_address).value_or("ERROR")
        << ", this: " << context;
  }

  // received both ipv4 and ipv6 responses, and no more responses (e.g. multiple
  // IP addresses for a domain name) are coming, finish `LookupHostname` resolve
  // with the collected results.
  if (!(flags & kDNSServiceFlagsMoreComing) && request.has_ipv4_response &&
      request.has_ipv6_response) {
    // extrace request and release lock before calling on_resolve
    auto request_node = that->requests_.extract(request_it);
    lock.Release();

    auto& request = request_node.mapped();
    if (request.result.empty()) {
      request.on_resolve(absl::NotFoundError(absl::StrFormat(
          "address lookup failed for %s: Domain name not found", hostname)));
    } else {
      request.on_resolve(std::move(request.result));
    }
    DNSServiceRefDeallocate(sdRef);
  }
}

void DNSServiceResolverImpl::Shutdown() {
  dispatch_async_f(queue_, Ref().release(), [](void* thatPtr) {
    grpc_core::RefCountedPtr<DNSServiceResolverImpl> that{
        static_cast<DNSServiceResolverImpl*>(thatPtr)};
    grpc_core::MutexLock lock(&that->request_mu_);
    for (auto& kv : that->requests_) {
      auto& sdRef = kv.first;
      auto& request = kv.second;
      GRPC_TRACE_LOG(event_engine_dns, INFO)
          << "DNSServiceResolverImpl::Shutdown sdRef: " << sdRef
          << ", this: " << thatPtr;
      request.on_resolve(
          absl::CancelledError("DNSServiceResolverImpl::Shutdown"));
      DNSServiceRefDeallocate(static_cast<DNSServiceRef>(sdRef));
    }
    that->requests_.clear();
  });
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE
