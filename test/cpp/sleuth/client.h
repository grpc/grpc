// Copyright 2025 gRPC authors.
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

#ifndef GRPC_TEST_CPP_SLEUTH_CLIENT_H
#define GRPC_TEST_CPP_SLEUTH_CLIENT_H

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <memory>
#include <vector>

#include "src/core/util/latent_see.h"
#include "src/proto/grpc/channelz/v2/latent_see.grpc.pb.h"
#include "src/proto/grpc/channelz/v2/service.grpc.pb.h"
#include "absl/status/statusor.h"

namespace grpc_sleuth {

class Client {
 public:
  struct Options {
    std::shared_ptr<grpc::ChannelCredentials> creds;
    std::string protocol = "h2";
  };

  Client(std::string target, Options options);

  absl::StatusOr<std::vector<grpc::channelz::v2::Entity>>
  QueryAllChannelzEntities();

  absl::StatusOr<std::vector<grpc::channelz::v2::Entity>>
  QueryAllChannelzEntitiesOfKind(absl::string_view entity_kind);

  absl::Status QueryTrace(
      int64_t entity_id, absl::string_view trace_name,
      absl::FunctionRef<
          void(size_t, absl::Span<const grpc::channelz::v2::TraceEvent* const>)>
          callback);
  absl::Status FetchLatentSee(double sample_time,
                              grpc_core::latent_see::Output* output);

 private:
  static grpc::ChannelArguments MakeChannelArguments(const Options& options);

  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<grpc::channelz::v2::Channelz::Stub> stub_;
  std::unique_ptr<grpc::channelz::v2::LatentSee::Stub> latent_see_stub_;
};

}  // namespace grpc_sleuth

#endif  // GRPC_TEST_CPP_SLEUTH_CLIENT_H
