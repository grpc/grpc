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

#include "src/core/ext/transport/chttp2/transport/max_concurrent_streams_policy.h"

#include <utility>

#include "absl/log/check.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

namespace grpc_core {

void Chttp2MaxConcurrentStreamsPolicy::AddDemerit() {
  ++new_demerits_;
  ++unacked_demerits_;
}

void Chttp2MaxConcurrentStreamsPolicy::FlushedSettings() {
  sent_demerits_ += std::exchange(new_demerits_, 0);
}

void Chttp2MaxConcurrentStreamsPolicy::AckLastSend() {
  CHECK(unacked_demerits_ >= sent_demerits_);
  unacked_demerits_ -= std::exchange(sent_demerits_, 0);
}

uint32_t Chttp2MaxConcurrentStreamsPolicy::AdvertiseValue() const {
  if (target_ < unacked_demerits_) return 0;
  return target_ - unacked_demerits_;
}

}  // namespace grpc_core
