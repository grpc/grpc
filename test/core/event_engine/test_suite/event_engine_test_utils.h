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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_UTILS_H_
#define GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_UTILS_H_

#include <functional>
#include <memory>
#include <string>
#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/iomgr/event_engine/promise.h"
#include "src/core/lib/resource_quota/memory_quota.h"

using EventEngineFactory = std::function<
    std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>;

using OnConnectCompleteCallback = std::function<void(absl::Status)>;

namespace grpc_event_engine {
namespace experimental {

void AppendStringToSliceBuffer(SliceBuffer* buf, std::string data);

std::string ExtractSliceBufferIntoString(SliceBuffer* buf);

// A helper class to create clients/listeners and connections between them.
// The clients and listeners can be created by the oracle event engine
// or the event engine under test. The class provides handles into the
// connections that are created. Inidividual tests can test expected behavior by
// exchanging arbitrary data over these connections.
class ConnectionManager {
 public:
  ConnectionManager(std::unique_ptr<EventEngine> test_event_engine,
                    std::unique_ptr<EventEngine> oracle_event_engine)
      : memory_quota_(std::make_unique<grpc_core::MemoryQuota>("foo")),
        test_event_engine_(std::move(test_event_engine)),
        oracle_event_engine_(std::move(oracle_event_engine)) {}
  ~ConnectionManager();

  // If successful, return OK status. It creates and starts a listener bound to
  // the specified address. The type of the listener is determined by the 2nd
  // argument.
  absl::Status StartListener(std::string addr, bool listener_type_oracle);

  // If connection is successful, returns a positive number which is a unique
  // connection id. The OnConnectCompleteCallback is invoked after the result
  // of the connection is known. If un-successfull it returns a non-OK
  // status containing the error encountered.
  absl::StatusOr<int> CreateConnection(std::string target_addr,
                                       absl::Time deadline,
                                       OnConnectCompleteCallback cb,
                                       bool client_type_oracle);

  // If client_endpoint is true, returns the client endpoint of the
  // corresponding connection_id. Otherwise, it returns the server endpoint.
  // A test may use these endpoints as it pleases but it does not get ownership
  // of the endpoint returned. The endpoint remains alive until explicitly
  // deleted using the CloseConnection method.
  EventEngine::Endpoint* GetEndpoint(int connection_id, bool client_endpoint);

  // A helper method to transfer data from client to server over the connection.
  // It returns an OK status after verifiying the server is able to fully
  // read the data that was written by the client.
  absl::Status TransferFromClient(int connection_id, std::string write_data) {
    return ExchangeData(connection_id, true, write_data);
  }

  // A helper method to transfer data from server to client over the connection.
  // It returns an OK status after verifiying the client is able to fully
  // read the data that was written by the server.
  absl::Status TransferFromServer(int connection_id, std::string write_data) {
    return ExchangeData(connection_id, false, write_data);
  }

  // Shuts down and deletes both endpoints of the specified connection.
  // Its safe to invoke this only when there is no ongoing ExchangeData
  // operations on the connection.
  void CloseConnection(int connection_id);

 private:
  // A helper method to exchange data over the connection. If send_from_client
  // is true, data is transfered from the client endpoint to the server endpoint
  // of the specific connection. Otherwise it is vise versa. The method returns
  // absl::OkStatus only after verifying that the data written at one end of the
  // connection equals data read by the other end of the connection.
  // It is safe to invoke this method from two separate threads for
  // for bi-directional data transfer.
  absl::Status ExchangeData(int connection_id, bool send_from_client,
                            std::string write_data);

  class Connection {
   public:
    Connection(int connection_id) : connection_id_(connection_id) {}
    ~Connection() = default;

    void SetClientEndpoint(
        std::unique_ptr<EventEngine::Endpoint>&& client_endpoint) {
      client_endpoint_promise_.Set(std::move(client_endpoint));
    }
    void SetServerEndpoint(
        std::unique_ptr<EventEngine::Endpoint>&& server_endpoint) {
      server_endpoint_promise_.Set(std::move(server_endpoint));
    }
    void WaitForClientEndpoint() {
      client_endpoint_ = std::move(client_endpoint_promise_.Get());
      client_endpoint_promise_.Reset();
    }
    void WaitForServerEndpoint() {
      server_endpoint_ = std::move(server_endpoint_promise_.Get());
      server_endpoint_promise_.Reset();
    }
    EventEngine::Endpoint* GetClientEndpoint() {
      return client_endpoint_.get();
    }
    EventEngine::Endpoint* GetServerEndpoint() {
      return server_endpoint_.get();
    }
    int GetConnectionId() { return connection_id_; }

   private:
    Promise<std::unique_ptr<EventEngine::Endpoint>> client_endpoint_promise_;
    Promise<std::unique_ptr<EventEngine::Endpoint>> server_endpoint_promise_;
    std::unique_ptr<EventEngine::Endpoint> client_endpoint_;
    std::unique_ptr<EventEngine::Endpoint> server_endpoint_;
    int connection_id_;
  };

  grpc_core::Mutex mu_;
  std::unique_ptr<grpc_core::MemoryQuota> memory_quota_;
  int num_processed_connections_ = 0;
  Connection* last_in_progress_connection_ = nullptr;
  std::map<std::string, std::unique_ptr<EventEngine::Listener>> listeners_;
  std::map<int, Connection*> connections_;
  std::unique_ptr<EventEngine> test_event_engine_;
  std::unique_ptr<EventEngine> oracle_event_engine_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_UTILS_H_
