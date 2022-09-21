// Copyright 2022 gRPC authors.
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

#include "test/core/event_engine/test_suite/event_engine_test_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/uri/uri_parser.h"

using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;

namespace grpc_event_engine {
namespace experimental {

EventEngine::ResolvedAddress URIToResolvedAddress(std::string address_str) {
  grpc_resolved_address addr;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(address_str);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "Failed to parse. Error: %s",
            uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  GPR_ASSERT(grpc_parse_uri(*uri, &addr));
  return EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(addr.addr), addr.len);
}

void AppendStringToSliceBuffer(SliceBuffer* buf, std::string data) {
  buf->Append(Slice::FromCopiedString(data));
}

std::string ExtractSliceBufferIntoString(SliceBuffer* buf) {
  if (!buf->Length()) {
    return std::string();
  }
  std::string tmp(buf->Length(), '\0');
  char* bytes = const_cast<char*>(tmp.c_str());
  grpc_slice_buffer_move_first_into_buffer(buf->c_slice_buffer(), buf->Length(),
                                           bytes);
  return tmp;
}

absl::Status SendValidatePayload(std::string data, Endpoint* send_endpoint,
                                 Endpoint* receive_endpoint) {
  GPR_ASSERT(receive_endpoint != nullptr && send_endpoint != nullptr);
  int num_bytes_written = data.size();
  grpc_core::Notification read_signal;
  grpc_core::Notification write_signal;
  SliceBuffer read_slice_buf;
  SliceBuffer write_slice_buf;

  AppendStringToSliceBuffer(&write_slice_buf, data);
  EventEngine::Endpoint::ReadArgs args = {num_bytes_written};
  std::function<void(absl::Status)> read_cb;
  read_cb = [receive_endpoint, &read_slice_buf, &read_cb, &read_signal,
             &args](absl::Status status) {
    GPR_ASSERT(status.ok());
    if (read_slice_buf.Length() == static_cast<size_t>(args.read_hint_bytes)) {
      read_signal.Notify();
      return;
    }
    args.read_hint_bytes -= read_slice_buf.Length();
    receive_endpoint->Read(std::move(read_cb), &read_slice_buf, &args);
  };
  // Start asynchronous reading at the receive_endpoint.
  receive_endpoint->Read(std::move(read_cb), &read_slice_buf, &args);
  // Start asynchronous writing at the send_endpoint.
  send_endpoint->Write(
      [&write_signal](absl::Status status) {
        GPR_ASSERT(status.ok());
        write_signal.Notify();
      },
      &write_slice_buf, nullptr);
  write_signal.WaitForNotification();
  read_signal.WaitForNotification();
  // Check if data written == data read
  if (data != ExtractSliceBufferIntoString(&read_slice_buf)) {
    return absl::CancelledError("Data read != Data written");
  }
  return absl::OkStatus();
}

absl::Status ConnectionManager::BindAndStartListener(
    std::vector<std::string> addrs, bool listener_type_oracle) {
  grpc_core::MutexLock lock(&mu_);
  if (addrs.empty()) {
    return absl::InvalidArgumentError(
        "Atleast one bind address must be specified");
  }
  for (auto& addr : addrs) {
    if (listeners_.find(addr) != listeners_.end()) {
      // There is already a listener at this address. Return error.
      return absl::AlreadyExistsError(
          absl::StrCat("Listener already existis for address: ", addr));
    }
  }
  Listener::AcceptCallback accept_cb =
      [this](std::unique_ptr<Endpoint> ep,
             MemoryAllocator /*memory_allocator*/) {
        last_in_progress_connection_.SetServerEndpoint(std::move(ep));
      };

  EventEngine* event_engine = listener_type_oracle ? oracle_event_engine_.get()
                                                   : test_event_engine_.get();

  ChannelArgsEndpointConfig config;
  auto status = event_engine->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { GPR_ASSERT(status.ok()); }, config,
      absl::make_unique<grpc_core::MemoryQuota>("foo"));
  if (!status.ok()) {
    return status.status();
  }

  std::shared_ptr<Listener> listener((*status).release());
  for (auto& addr : addrs) {
    auto bind_status = listener->Bind(URIToResolvedAddress(addr));
    if (!bind_status.ok()) {
      gpr_log(GPR_ERROR, "Binding listener failed: %s",
              bind_status.status().ToString().c_str());
      return bind_status.status();
    }
  }
  GPR_ASSERT(listener->Start().ok());
  // Insert same listener pointer for all bind addresses after the listener
  // has started successfully.
  for (auto& addr : addrs) {
    listeners_.insert(std::make_pair(addr, listener));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::tuple<std::unique_ptr<Endpoint>, std::unique_ptr<Endpoint>>>
ConnectionManager::CreateConnection(std::string target_addr,
                                    EventEngine::Duration timeout,
                                    bool client_type_oracle) {
  // Only allow one CreateConnection call to proceed at a time.
  grpc_core::MutexLock lock(&mu_);
  std::string conn_name =
      absl::StrCat("connection-", std::to_string(num_processed_connections_++));
  EventEngine* event_engine = client_type_oracle ? oracle_event_engine_.get()
                                                 : test_event_engine_.get();
  ChannelArgsEndpointConfig config;
  event_engine->Connect(
      [this](absl::StatusOr<std::unique_ptr<Endpoint>> status) {
        if (!status.ok()) {
          gpr_log(GPR_ERROR, "Connect failed: %s",
                  status.status().ToString().c_str());
          last_in_progress_connection_.SetClientEndpoint(nullptr);
        } else {
          last_in_progress_connection_.SetClientEndpoint(std::move(*status));
        }
      },
      URIToResolvedAddress(target_addr), config,
      memory_quota_->CreateMemoryAllocator(conn_name), timeout);

  auto client_endpoint = last_in_progress_connection_.GetClientEndpoint();
  if (client_endpoint != nullptr &&
      listeners_.find(target_addr) != listeners_.end()) {
    // There is a listener for the specified address. Wait until it
    // creates a ServerEndpoint after accepting the connection.
    auto server_endpoint = last_in_progress_connection_.GetServerEndpoint();
    GPR_ASSERT(server_endpoint != nullptr);
    // Set last_in_progress_connection_ to nullptr
    return std::make_tuple(std::move(client_endpoint),
                           std::move(server_endpoint));
  }
  return absl::CancelledError("Failed to create connection.");
}

}  // namespace experimental
}  // namespace grpc_event_engine
