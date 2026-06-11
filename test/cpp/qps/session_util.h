// Copyright 2024 gRPC authors.
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

#ifndef GRPC_TEST_CPP_QPS_SESSION_UTIL_H
#define GRPC_TEST_CPP_QPS_SESSION_UTIL_H

#include <grpcpp/alarm.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>

#include <memory>
#include <string>

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {

class OuterSessionService : public grpc::Service {
 public:
  explicit OuterSessionService(grpc::Service* inner_service);
};

class SessionHolder {
 public:
  SessionHolder(std::shared_ptr<Channel> virtual_channel,
                std::unique_ptr<ClientContext> context,
                std::shared_ptr<absl::Notification> done);
  ~SessionHolder();

  std::shared_ptr<Channel> virtual_channel() const { return virtual_channel_; }
  void Close();

 private:
  std::shared_ptr<Channel> virtual_channel_;
  std::unique_ptr<ClientContext> context_;
  std::shared_ptr<absl::Notification> done_;
};

std::unique_ptr<SessionHolder> EstablishSession(
    std::shared_ptr<Channel> channel);

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_QPS_SESSION_UTIL_H
