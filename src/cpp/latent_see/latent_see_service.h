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

#ifndef GRPC_SRC_CPP_SERVER_LATENT_SEE_LATENT_SEE_SERVICE_H
#define GRPC_SRC_CPP_SERVER_LATENT_SEE_LATENT_SEE_SERVICE_H

#include "src/proto/grpc/latent_see/latent_see.grpc.pb.h"

namespace grpc {

class LatentSeeService final : public latent_see::v1::LatentSee::Service {
 public:
  struct Options {
    double max_query_time = 1.0;
    size_t max_memory = 1024 * 1024;

    Options& set_max_query_time(double max_query_time) {
      this->max_query_time = max_query_time;
      return *this;
    }
    Options& set_max_memory(size_t max_memory) {
      this->max_memory = max_memory;
      return *this;
    }
  };

  explicit LatentSeeService(const Options& options) : options_(options) {}

  Status GetTrace(
      ServerContext*, const latent_see::v1::GetTraceRequest* request,
      ServerWriter<latent_see::v1::LatentSeeTrace>* response) override;

 private:
  Options options_;
};

}  // namespace grpc

#endif
