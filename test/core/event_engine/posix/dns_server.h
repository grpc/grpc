// Copyright 2025 The gRPC Authors
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
#ifndef GRPC_TEST_CORE_EVENT_ENGINE_POSIX_DNS_SERVER_H
#define GRPC_TEST_CORE_EVENT_ENGINE_POSIX_DNS_SERVER_H

#include <grpc/event_engine/port.h>
#include <grpc/support/port_platform.h>

#include <array>
#include <cstdint>
#include <string>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "src/core/util/notification.h"

namespace grpc_event_engine::experimental {

struct DnsQuestion {
  uint16_t id;
  std::string qname;
  uint16_t qtype;
  uint16_t qclass;
  sockaddr_in client_addr;

  bool is_host(absl::string_view host) const {
    return absl::StartsWith(qname, host);
  }
};

class DnsServer {
 public:
  static absl::StatusOr<DnsServer> Start(int port);

  DnsServer(int port, int sockfd);
  ~DnsServer();
  std::string address() const;
  DnsQuestion WaitForQuestion(absl::string_view host) const;
  // IPv4 address as 4 bytes. This address will be returned x4 for IPv6.
  void SetIPv4Response(absl::string_view host,
                       absl::Span<const uint8_t> ipv4_address);

 private:
  void ServerLoop(int sockfd);
  absl::Status Respond(const DnsQuestion& query,
                       absl::Span<const uint8_t> ipv4_address);

  int port_;
  int sockfd_;
  grpc_core::Notification done_;
  grpc_core::Notification running_;
  mutable grpc_core::Mutex mu_;
  mutable grpc_core::CondVar cond_;
  absl::flat_hash_map<std::string, std::array<uint8_t, 4>> ipv4_addresses_
      ABSL_GUARDED_BY(mu_);
  absl::InlinedVector<DnsQuestion, 16> questions_ ABSL_GUARDED_BY(mu_);
  std::thread background_thread_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_POSIX_DNS_SERVER_H