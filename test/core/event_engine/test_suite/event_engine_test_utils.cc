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
#include "absl/time/time.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/uri/uri_parser.h"

using ResolvedAddress =
    grpc_event_engine::experimental::EventEngine::ResolvedAddress;
using Endpoint = grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = grpc_event_engine::experimental::EventEngine::Listener;

namespace grpc_event_engine {
namespace experimental {

namespace {

ResolvedAddress URIToResolvedAddress(std::string address_str) {
  grpc_resolved_address addr;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(address_str);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "Failed to parse. Error: %s",
            uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  GPR_ASSERT(grpc_parse_uri(*uri, &addr));
  return grpc_event_engine::experimental::EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(addr.addr), addr.len);
}

}  // namespace

void AppendStringToSliceBuffer(SliceBuffer* buf, std::string data) {
  buf->Append(Slice::FromCopiedString(data));
}

std::string ExtractSliceBufferIntoString(SliceBuffer* buf) {
  if (!buf->Length()) {
    return std::string();
  }
  std::string tmp(buf->Length(), '\0');
  char* bytes = const_cast<char*>(tmp.c_str());
  grpc_slice_buffer_move_first_into_buffer(buf->RawSliceBuffer(), buf->Length(),
                                           bytes);
  return tmp;
}

class TestEndpointConfig : public EndpointConfig {
 public:
  EndpointConfig::Setting Get(absl::string_view /*key*/) const override {
    return nullptr;
  }
};

absl::Status ConnectionManager::StartListener(std::string addr,
                                              bool listener_type_oracle) {
  grpc_core::MutexLock lock(&mu_);
  if (listeners_.find(addr) != listeners_.end()) {
    // There is already a listener at this address. Return false.
    return absl::AlreadyExistsError(
        "Listener already existis for specified address");
  }
  Listener::AcceptCallback accept_cb =
      [this](std::unique_ptr<Endpoint> ep,
             MemoryAllocator /*memory_allocator*/) {
        GPR_ASSERT(last_in_progress_connection_ != nullptr);
        last_in_progress_connection_->SetServerEndpoint(std::move(ep));
      };

  std::unique_ptr<Listener> listener;
  EventEngine* event_engine = listener_type_oracle ? oracle_event_engine_.get()
                                                   : test_event_engine_.get();

  auto status = event_engine->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { GPR_ASSERT(status.ok()); },
      TestEndpointConfig{}, std::make_unique<grpc_core::MemoryQuota>("foo"));
  if (!status.ok()) {
    return status.status();
  }
  listener = std::move(*status);
  // Bind the newly created listener to the specified address.
  auto bind_status = listener->Bind(URIToResolvedAddress(addr));
  if (bind_status.ok()) {
    // If the bind was successfull, start the listener immediately.
    GPR_ASSERT(listener->Start().ok());
    listeners_.insert(std::make_pair(addr, std::move(listener)));
  }  // else the newly created listener will automatically get deleted.

  return bind_status.ok() ? absl::OkStatus() : bind_status.status();
}

ConnectionManager::~ConnectionManager() {
  for (auto it = connections_.begin(); it != connections_.end(); it++) {
    // Close both endpoints of active connection.
    delete it->second;
  }
  connections_.clear();
  // Automatically trigger shutdown of all listeners because the listener object
  // should get deleted.
  listeners_.clear();
}

absl::StatusOr<int> ConnectionManager::CreateConnection(
    std::string target_addr, absl::Time deadline, OnConnectCompleteCallback cb,
    bool client_type_oracle) {
  // Only allow one CreateConnection call to proceed at a time.
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(last_in_progress_connection_ == nullptr);
  last_in_progress_connection_ = new Connection(num_processed_connections_++);
  std::string conn_name = absl::StrCat(
      "connection-",
      std::to_string(last_in_progress_connection_->GetConnectionId()));
  EventEngine* event_engine = client_type_oracle ? oracle_event_engine_.get()
                                                 : test_event_engine_.get();
  event_engine->Connect(
      [this, cb](absl::StatusOr<std::unique_ptr<Endpoint>> status) {
        if (cb != nullptr) {
          cb(status.status());
        }
        if (!status.ok()) {
          last_in_progress_connection_->SetClientEndpoint(nullptr);
        } else {
          last_in_progress_connection_->SetClientEndpoint(std::move(*status));
        }
      },
      URIToResolvedAddress(target_addr), TestEndpointConfig{},
      memory_quota_->CreateMemoryAllocator(conn_name), deadline);

  last_in_progress_connection_->WaitForClientEndpoint();
  if (last_in_progress_connection_->GetClientEndpoint() &&
      listeners_.find(target_addr) != listeners_.end()) {
    // There is a listener for the specified address. Wait until it
    // creates a ServerEndpoint after accepting the connection.
    last_in_progress_connection_->WaitForServerEndpoint();
    GPR_ASSERT(last_in_progress_connection_->GetClientEndpoint() != nullptr);
    GPR_ASSERT(last_in_progress_connection_->GetServerEndpoint() != nullptr);
    connections_.insert(
        std::make_pair(last_in_progress_connection_->GetConnectionId(),
                       last_in_progress_connection_));
    // Set last_in_progress_connection_ to nullptr
    return absl::exchange(last_in_progress_connection_, nullptr)
        ->GetConnectionId();
  }
  GPR_ASSERT(last_in_progress_connection_->GetClientEndpoint() == nullptr);
  delete last_in_progress_connection_;
  last_in_progress_connection_ = nullptr;
  return absl::CancelledError("Failed to create connection.");
}

Endpoint* ConnectionManager::GetEndpoint(int connection_id,
                                         bool client_endpoint) {
  grpc_core::MutexLock lock(&mu_);
  auto it = connections_.find(connection_id);
  if (it == connections_.end()) {
    return nullptr;
  }
  return client_endpoint ? it->second->GetClientEndpoint()
                         : it->second->GetServerEndpoint();
}

absl::Status ConnectionManager::ExchangeData(int connection_id,
                                             bool send_from_client,
                                             std::string write_data) {
  Endpoint* send_endpoint = nullptr;
  Endpoint* receive_endpoint = nullptr;
  {
    grpc_core::MutexLock lock(&mu_);
    auto it = connections_.find(connection_id);
    if (it == connections_.end() || write_data.empty()) {
      return absl::InvalidArgumentError("One or more arguments are invalid!");
    }
    send_endpoint = send_from_client ? it->second->GetClientEndpoint()
                                     : it->second->GetServerEndpoint();
    receive_endpoint = send_from_client ? it->second->GetServerEndpoint()
                                        : it->second->GetClientEndpoint();
  }
  GPR_ASSERT(receive_endpoint != nullptr && send_endpoint != nullptr);
  int num_bytes_written = write_data.size();
  Promise<bool> read_promise;
  Promise<bool> write_promise;
  SliceBuffer read_slice_buf;
  SliceBuffer write_slice_buf;

  AppendStringToSliceBuffer(&write_slice_buf, write_data);
  EventEngine::Endpoint::ReadArgs args = {num_bytes_written};
  std::function<void(absl::Status)> read_cb;
  read_cb = [receive_endpoint, &read_slice_buf, &read_cb, &read_promise,
             &args](absl::Status status) {
    GPR_ASSERT(status.ok());
    if (read_slice_buf.Length() == args.read_hint_bytes) {
      read_promise.Set(true);
      return;
    }
    args.read_hint_bytes -= read_slice_buf.Length();
    receive_endpoint->Read(std::move(read_cb), &read_slice_buf, &args);
  };
  // Start asynchronous reading at the receive_endpoint.
  receive_endpoint->Read(std::move(read_cb), &read_slice_buf, &args);
  // Start asynchronous writing at the send_endpoint.
  send_endpoint->Write(
      [&write_promise](absl::Status status) {
        GPR_ASSERT(status.ok());
        write_promise.Set(true);
      },
      &write_slice_buf, nullptr);
  // Wait for async write to complete.
  GPR_ASSERT(write_promise.Get() == true);
  // Wait for async read to complete.
  GPR_ASSERT(read_promise.Get() == true);
  // Check if data written == data read
  if (write_data != ExtractSliceBufferIntoString(&read_slice_buf)) {
    return absl::CancelledError("Data read != Data written");
  }
  return absl::OkStatus();
}

// Shuts down both endpoints of the connection.
void ConnectionManager::CloseConnection(int connection_id) {
  grpc_core::MutexLock lock(&mu_);
  auto it = connections_.find(connection_id);
  if (it == connections_.end()) {
    return;
  }
  delete it->second;
  connections_.erase(it);
}

}  // namespace experimental
}  // namespace grpc_event_engine
