/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef TEST_CPP_MICROBENCHMARKS_COUNTERS_H
#define TEST_CPP_MICROBENCHMARKS_COUNTERS_H

#include <grpc/support/port_platform.h>

#include <sstream>
#include <vector>

#include <benchmark/benchmark.h>

#include <grpcpp/impl/grpc_library.h>

#include "src/core/lib/debug/stats.h"

class LibraryInitializer {
 public:
  LibraryInitializer();
  ~LibraryInitializer();

  static LibraryInitializer& get();

 private:
  grpc::internal::GrpcLibrary init_lib_;
};

class TrackCounters {
 public:
  TrackCounters() { grpc_stats_collect(&stats_begin_); }
  virtual ~TrackCounters() {}
  virtual void Finish(benchmark::State& state);
  virtual void AddLabel(const std::string& label);
  virtual void AddToLabel(std::ostream& out, benchmark::State& state);

 private:
  grpc_stats_data stats_begin_;
  std::vector<std::string> labels_;
};

#endif
