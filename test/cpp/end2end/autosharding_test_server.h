//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CPP_END2END_AUTOSHARDING_TEST_SERVER_H
#define GRPC_TEST_CPP_END2END_AUTOSHARDING_TEST_SERVER_H

#include <grpcpp/server.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/status.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "proto/autosharding/v1/autosharding.grpc.pb.h"
#include "proto/autosharding/v1/autosharding.pb.h"
#include "src/core/util/sync.h"
#include "test/cpp/end2end/counted_service.h"
#include "absl/base/thread_annotations.h"

namespace grpc {
namespace testing {

// A fake AutoshardingService (gRFC A119) that sends scripted assignments.
//
// Each WatchShardingAssignment stream gets the latest assignment when it
// starts, and every assignment that the test sets after that.
class AutoshardingServiceImpl
    : public CountedService<google::cloud::autosharding::v1::
                                AutoshardingService::CallbackService> {
 public:
  using Request =
      google::cloud::autosharding::v1::WatchShardingAssignmentRequest;
  using Response =
      google::cloud::autosharding::v1::WatchShardingAssignmentResponse;

  // A slice of the keyspace.  The slice starts at the end_key of the
  // previous slice ("" for the first slice).  An empty end_key means
  // +infinity.  endpoint_indices are indices into the endpoint names of the
  // assignment.
  struct Slice {
    std::string end_key;
    std::vector<int32_t> endpoint_indices;
  };

  // Callback-based handler (gRFC A119).  Reads and writes are driven by the
  // gRPC library, so no per-RPC thread is needed.
  grpc::ServerBidiReactor<Request, Response>* WatchShardingAssignment(
      grpc::CallbackServerContext* context) override;

  // Called by AutoshardingTestServer before the server starts and before it
  // shuts down.  Shutdown() ends all open streams.
  void Start();
  void Shutdown();

  // Sends an assignment, as one chunk followed by its metadata, on every
  // open stream and on every stream that starts later.
  void SetAssignment(const std::vector<Slice>& slices,
                     const std::vector<std::string>& endpoint_names,
                     int64_t generation);

  // Streams whose InitialClientConfig target is target do not get any
  // assignment.  They still read the requests of the client.
  void SetSilentTarget(std::string target);

  // Returns the target of the InitialClientConfig of each stream.
  std::vector<std::string> targets();

 private:
  class Reactor;

  grpc_core::Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  std::string silent_target_ ABSL_GUARDED_BY(mu_);
  // The latest assignment, and a version that increases when it changes.
  std::vector<Response> assignment_ ABSL_GUARDED_BY(mu_);
  uint64_t assignment_version_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::string> targets_ ABSL_GUARDED_BY(mu_);
  // Active streams.  Each Reactor removes itself in OnDone().
  std::set<Reactor*> reactors_ ABSL_GUARDED_BY(mu_);
};

// Runs the fake AutoshardingServiceImpl on an insecure listening port on
// the local IP address.  The name differs from XdsEnd2endTest::ServerThread
// so that xDS tests can use this class.
class AutoshardingTestServer {
 public:
  AutoshardingTestServer();
  ~AutoshardingTestServer() { Shutdown(); }

  // Starts the server.  After Shutdown(), Start() restarts it on the same
  // port.
  void Start();
  void Shutdown();

  int port() const { return port_; }
  AutoshardingServiceImpl& service() { return service_; }

 private:
  const int port_;
  AutoshardingServiceImpl service_;
  std::unique_ptr<Server> server_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_AUTOSHARDING_TEST_SERVER_H
