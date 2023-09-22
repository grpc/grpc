// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CORE_UTIL_PROTO_BIT_GEN_H
#define GRPC_TEST_CORE_UTIL_PROTO_BIT_GEN_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace grpc_core {

// Set of random numbers from a proto file (or other container) forming a bit
// source. Satisfies the requirements for a URNG.
class ProtoBitGen : public std::numeric_limits<uint64_t> {
 public:
  template <typename SourceContainer>
  explicit ProtoBitGen(const SourceContainer& c) {
    for (auto r : c) {
      results_.push_back(r);
    }
  }

  using result_type = uint64_t;

  uint64_t operator()() {
    if (results_.empty()) {
      ++current_;
      return current_;
    }
    // We loop through but increment by one each round, to guarantee to see all
    // values eventually.
    uint64_t out =
        results_[current_ % results_.size()] + (current_ / results_.size());
    ++current_;
    return out;
  }

 private:
  std::vector<uint64_t> results_;
  size_t current_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_PROTO_BIT_GEN_H
