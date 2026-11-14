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

#include <grpc/support/time.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/sync_stream.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "proto/autosharding/v1/autosharding.grpc.pb.h"
#include "proto/autosharding/v1/autosharding.pb.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/counted_service.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace testing {

// A fake AutoshardingService (gRFC A119) that sends scripted assignments.
//
// Each WatchShardingAssignment stream gets the latest assignment when it
// starts, and every assignment that the test sets after that.
class AutoshardingServiceImpl
    : public CountedService<
          google::cloud::autosharding::v1::AutoshardingService::Service> {
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

  Status WatchShardingAssignment(
      ServerContext* context,
      ServerReaderWriter<Response, Request>* stream) override;

  // Called by the server thread before the server starts and before it shuts
  // down.  Shutdown() ends all open streams.
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
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  std::string silent_target_ ABSL_GUARDED_BY(mu_);
  // The latest assignment, and a version that increases when it changes.
  std::vector<Response> assignment_ ABSL_GUARDED_BY(mu_);
  uint64_t assignment_version_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::string> targets_ ABSL_GUARDED_BY(mu_);
};

// Runs a server with an insecure listening port on the local IP address.
// ServiceImpl must have Start() and Shutdown() methods.
template <typename ServiceImpl>
class ServerThread {
 public:
  explicit ServerThread(absl::string_view type)
      : type_(type), port_(grpc_pick_unused_port_or_die()) {}

  ~ServerThread() { Shutdown(); }

  // Starts the server.  After Shutdown(), Start() restarts it on the same
  // port.
  void Start() {
    LOG(INFO) << "starting " << type_ << " server on port " << port_;
    service_.Start();
    ServerBuilder builder;
    builder.AddListeningPort(grpc_core::LocalIpAndPort(port_),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    GRPC_CHECK(server_ != nullptr);
  }

  void Shutdown() {
    if (server_ == nullptr) return;
    LOG(INFO) << "shutting down " << type_ << " server on port " << port_;
    service_.Shutdown();
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    server_.reset();
  }

  int port() const { return port_; }
  ServiceImpl& service() { return service_; }

 private:
  const std::string type_;
  const int port_;
  ServiceImpl service_;
  std::unique_ptr<Server> server_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_AUTOSHARDING_TEST_SERVER_H
