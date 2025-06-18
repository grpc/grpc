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

#include "test/core/event_engine/posix/dns_server.h"

#include <string>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include <fcntl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <thread>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/substitute.h"
#include "src/core/util/notification.h"

namespace grpc_event_engine::experimental {

namespace {

std::string ParseQName(absl::Span<const uint8_t> buffer, size_t& pos) {
  absl::InlinedVector<std::string, 10> qname;
  size_t label_length = static_cast<size_t>(buffer[pos++]);
  while (label_length != 0) {
    auto range = buffer.subspan(pos, label_length);
    std::string s(range.begin(), range.end());
    qname.emplace_back(std::move(s));
    pos += label_length;
    label_length = static_cast<size_t>(buffer[pos++]);
  }
  return absl::StrJoin(qname, ".");
}

class BytePacker {
 public:
  BytePacker& Pack8(uint8_t v) {
    data_.emplace_back(v);
    return *this;
  }

  BytePacker& Pack16(uint16_t value) { return PackMultiByte(htons(value)); }

  BytePacker& Pack32(uint32_t value) { return PackMultiByte(htonl(value)); }

  std::vector<uint8_t> data() const { return data_; }

  BytePacker& PackArray(absl::Span<const uint8_t> data) {
    Pack16(data.size());
    std::copy(data.begin(), data.end(), std::back_inserter(data_));
    return *this;
  }

  BytePacker& PackQName(absl::string_view qname) {
    for (absl::string_view segment : absl::StrSplit(qname, '.')) {
      Pack8(segment.size());
      std::copy(segment.begin(), segment.end(), std::back_inserter(data_));
    }
    Pack8(0x00);
    return *this;
  }

 private:
  template <typename T>
  BytePacker& PackMultiByte(T v) {
    const uint8_t* start = reinterpret_cast<const uint8_t*>(&v);
    std::copy(start, start + sizeof(T), std::back_inserter(data_));
    return *this;
  }

  std::vector<uint8_t> data_;
};

class ByteUnpacker {
 public:
  explicit ByteUnpacker(absl::Span<const uint8_t> data) : data_(data) {}

  ByteUnpacker& Expect16(uint16_t expected, absl::string_view name) {
    auto value = Read2(name);
    if (value.has_value() && *value != expected) {
      status_ = absl::InvalidArgumentError(absl::Substitute(
          "Filed $0: expected: $1, got: $2", name, expected, *value));
    }
    return *this;
  }

  absl::StatusOr<DnsQuestion> query() const {
    if (!status_.ok()) {
      return status_;
    }
    return query_;
  }

  ByteUnpacker& Skip16(absl::string_view name) {
    Read2(name);
    return *this;
  }

  ByteUnpacker& Unpack(uint16_t DnsQuestion::* field, absl::string_view name) {
    auto value = Read2(name);
    if (value.has_value()) {
      query_.*field = *value;
    }
    return *this;
  }

  ByteUnpacker& Unpack(std::string DnsQuestion::* field) {
    if (!status_.ok()) return *this;
    query_.*field = ParseQName(data_, pos_);
    return *this;
  }

 private:
  std::optional<uint16_t> Read2(absl::string_view name) {
    if (!status_.ok()) return std::nullopt;
    if (data_.size() < pos_ + 2) {
      status_ = absl::InvalidArgumentError(
          absl::Substitute("Not enough bytes for $0", name));
      return std::nullopt;
    }
    uint16_t value =
        (static_cast<uint16_t>(data_[pos_]) << 8) + data_[pos_ + 1];
    pos_ += 2;
    // Note that this is network byte order. Not casting pointers to avoid
    // UB with unaligned reads.
    return value;
  }

  absl::Span<const uint8_t> data_;
  DnsQuestion query_;
  size_t pos_ = 0;
  absl::Status status_;
};

absl::StatusOr<DnsQuestion> ParseQuestion(absl::Span<const uint8_t> buffer) {
  return ByteUnpacker(buffer)
      .Unpack(&DnsQuestion::id, "ID")
      // Fields below are ignored for now
      .Skip16("FLAGS")
      .Expect16(1, "QDCOUNT")
      .Expect16(0, "ANCOUNT")
      .Expect16(0, "NSCOUNT")
      .Expect16(0, "ARCOUNT")
      .Unpack(&DnsQuestion::qname)
      .Unpack(&DnsQuestion::qtype, "QTYPE")
      .Unpack(&DnsQuestion::qclass, "QCLASS")
      .query();
}

std::vector<unsigned char> FormatAnswer(const DnsQuestion& query,
                                        absl::Span<const uint8_t> address) {
  return BytePacker()
      .Pack16(query.id)        // ID
      .Pack16(0x8000)          // FLAGS
      .Pack16(1)               // QDCOUNT
      .Pack16(1)               // ANCOUNT
      .Pack16(0)               // NSCOUNT
      .Pack16(0)               // ARCOUNT
      .PackQName(query.qname)  // Query QNAME
      .Pack16(query.qtype)     // QTYPE
      .Pack16(query.qclass)    // QCLASS
      .Pack16(0xC00C)          // Answer QNAME - pointer
      .Pack16(query.qtype)     // QTYPE
      .Pack16(query.qclass)    // QCLASS
      .Pack32(2000)            // TTL
      .PackArray(address)
      .data();
}

std::array<uint8_t, 16> ToIPv6Address(absl::Span<const uint8_t> ipv4_address) {
  std::array<uint8_t, 16> ipv6_address;
  std::copy(ipv4_address.begin(), ipv4_address.end(), ipv6_address.begin());
  std::copy(ipv4_address.begin(), ipv4_address.end(), ipv6_address.begin() + 4);
  std::copy(ipv4_address.begin(), ipv4_address.end(), ipv6_address.begin() + 8);
  std::copy(ipv4_address.begin(), ipv4_address.end(),
            ipv6_address.begin() + 12);
  return ipv6_address;
}

}  // namespace

DnsServer::DnsServer(int port, int sockfd)
    : port_(port),
      sockfd_(sockfd),
      background_thread_(&DnsServer::ServerLoop, this, sockfd) {
  running_.WaitForNotification();
}

DnsServer::~DnsServer() {
  done_.Notify();
  background_thread_.join();
}

std::string DnsServer::address() const {
  return absl::StrCat("127.0.0.1:", port_);
}

DnsQuestion DnsServer::WaitForQuestion(absl::string_view host) const {
  grpc_core::MutexLock lock(&mu_);
  while (std::find_if(questions_.begin(), questions_.end(), [=](const auto& q) {
           return q.is_host(host);
         }) == questions_.end()) {
    cond_.WaitWithTimeout(&mu_, absl::Milliseconds(50));
  }
  return *std::find_if(questions_.begin(), questions_.end(),
                       [=](const auto& q) { return q.is_host(host); });
}

absl::Status DnsServer::Respond(const DnsQuestion& query,
                                absl::Span<const uint8_t> ipv4_address) {
  LOG(INFO) << "Answering question " << query.id << " for domain "
            << query.qname << " type: " << query.qtype;
  auto packet = FormatAnswer(
      query, query.qtype == 1 ? ipv4_address : ToIPv6Address(ipv4_address));
  auto sent = sendto(sockfd_, packet.data(), packet.size(), 0,
                     reinterpret_cast<const sockaddr*>(&query.client_addr),
                     sizeof(query.client_addr));
  if (sent < 0) {
    return absl::ErrnoToStatus(errno, "Sending response");
  }
  return absl::OkStatus();
}

void DnsServer::SetIPv4Response(absl::string_view host,
                                absl::Span<const uint8_t> ipv4_address) {
  CHECK_EQ(ipv4_address.size(), 4u);
  grpc_core::MutexLock lock(&mu_);
  for (const auto& question : questions_) {
    if (question.is_host(host)) {
      auto status = Respond(question, ipv4_address);
      LOG_IF(FATAL, !status.ok()) << status;
    }
  }
  questions_.clear();
  std::copy(ipv4_address.begin(), ipv4_address.end(),
            ipv4_addresses_[host].begin());
}

void DnsServer::ServerLoop(int sockfd) {
  running_.Notify();
  std::array<uint8_t, 2048> buffer;
  sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  absl::Cleanup server_cleanup = [&, sockfd]() {
    LOG(INFO) << "DNS server shutdown: " << done_.HasBeenNotified();
    close(sockfd);
  };
  while (!done_.HasBeenNotified()) {
    auto received_bytes = recvfrom(sockfd, buffer.data(), buffer.size(), 0,
                                   (struct sockaddr*)&client_addr, &client_len);
    if (received_bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      absl::SleepFor(absl::Milliseconds(5));
      continue;
    }
    if (received_bytes < 0) {
      LOG(FATAL) << absl::ErrnoToStatus(errno, "Reading from socket");
      return;
    }
    auto query =
        ParseQuestion(absl::Span<const uint8_t>(buffer).first(received_bytes));
    if (!query.ok()) {
      LOG(FATAL) << query.status();
    }
    LOG(INFO) << "Received question " << query->id << " for domain "
              << query->qname;
    query->client_addr = client_addr;
    {
      grpc_core::MutexLock lock(&mu_);
      bool responded = false;
      for (const auto& [host, address] : ipv4_addresses_) {
        LOG(INFO) << query->qname << " " << host << " " << query->is_host(host);
        if (query->is_host(host)) {
          auto response = Respond(*query, address);
          LOG_IF(FATAL, !response.ok()) << response;
          responded = true;
          break;
        }
      }
      if (!responded) {
        questions_.emplace_back(std::move(query).value());
        cond_.SignalAll();
      }
    }
  }
}

absl::StatusOr<DnsServer> DnsServer::Start(int port) {
  bool success = false;
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd > 0) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
      success = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == 0;
    }
  }
  if (!success) {
    return absl::ErrnoToStatus(errno, "Error creating socket");
  }
  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    auto status = absl::ErrnoToStatus(errno, "Error binding socket");
    close(sockfd);
    return status;
  }
  LOG(INFO) << "DNS server listening on port " << port;
  return absl::StatusOr<DnsServer>(absl::in_place, port, sockfd);
}

}  // namespace grpc_event_engine::experimental

#else  // GRPC_POSIX_SOCKET

#include "src/core/util/crash.h"

namespace grpc_event_engine::experimental {

absl::StatusOr<DnsServer> DnsServer::Start(int port) {
  return absl::UnimplementedError("Unsupported platform");
}

DnsServer::~DnsServer() = default;

std::string DnsServer::address() const {
  grpc_core::Crash("Unsupported platform");
}

DnsQuestion DnsServer::WaitForQuestion(absl::string_view /*host*/) const {
  grpc_core::Crash("Unsupported platform");
}

void DnsServer::SetIPv4Response(absl::string_view /*host*/,
                                absl::Span<const uint8_t> /*ipv4_address*/) {
  grpc_core::Crash("Unsupported platform");
}

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_POSIX_SOCKET