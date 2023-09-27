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

#ifndef GRPC_MAX_CONCURRENT_STREAMS_POLICY_H
#define GRPC_MAX_CONCURRENT_STREAMS_POLICY_H

#include <grpc/impl/codegen/port_platform.h>

#include <cstdint>
#include <limits>

namespace grpc_core {

class Chttp2MaxConcurrentStreamsPolicy {
 public:
  void SetTarget(uint32_t target);
  void AddDemerit();
  void FlushedSettings();
  void AckLastSend();
  uint32_t ActiveTarget();

 private:
  uint32_t target_ = std::numeric_limits<int32_t>::max();
  uint32_t new_demerits_ = 0;
  uint32_t sent_demerits_ = 0;
  uint32_t unacked_demerits_ = 0;
};

}  // namespace grpc_core

#endif
