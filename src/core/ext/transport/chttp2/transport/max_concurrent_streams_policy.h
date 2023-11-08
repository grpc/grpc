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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_MAX_CONCURRENT_STREAMS_POLICY_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_MAX_CONCURRENT_STREAMS_POLICY_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <limits>

namespace grpc_core {

class Chttp2MaxConcurrentStreamsPolicy {
 public:
  // Set the target number of concurrent streams.
  // If everything is idle we should advertise this number.
  void SetTarget(uint32_t target) { target_ = target; }

  // Add one demerit to the current target.
  // We need to do one full settings round trip after this to clear this
  // demerit.
  // It will reduce our advertised max concurrent streams by one.
  void AddDemerit();

  // Notify the policy that we've sent a settings frame.
  // Newly added demerits since the last settings frame was sent will be cleared
  // once that settings frame is acknowledged.
  void FlushedSettings();

  // Notify the policy that we've received an acknowledgement for the last
  // settings frame we sent.
  void AckLastSend();

  // Returns what we should advertise as max concurrent streams.
  uint32_t AdvertiseValue() const;

 private:
  uint32_t target_ = std::numeric_limits<int32_t>::max();
  // Demerit flow:
  // When we add a demerit, we add to both new & unacked.
  // When we flush settings, we move new to sent.
  // When we ack settings, we remove what we sent from unacked.
  // eg:
  // we add 10 demerits - now new=10, sent=0, unacked=10
  // we send settings - now new=0, sent=10, unacked=10
  // we add 5 demerits - now new=5, sent=10, unacked=15
  // we get the settings ack - now new=5, sent=0, unacked=5
  uint32_t new_demerits_ = 0;
  uint32_t sent_demerits_ = 0;
  uint32_t unacked_demerits_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_MAX_CONCURRENT_STREAMS_POLICY_H
