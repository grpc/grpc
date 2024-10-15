//
//
// Copyright 2017 gRPC authors.
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
//

#ifndef GRPC_TEST_CPP_MICROBENCHMARKS_HELPERS_H
#define GRPC_TEST_CPP_MICROBENCHMARKS_HELPERS_H

#include <benchmark/benchmark.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/impl/grpc_library.h>

#include <sstream>
#include <vector>

#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"

class LibraryInitializer {
 public:
  LibraryInitializer();
  ~LibraryInitializer();

  static LibraryInitializer& get();

 private:
  grpc::internal::GrpcLibrary init_lib_;
};

#endif  // GRPC_TEST_CPP_MICROBENCHMARKS_HELPERS_H
