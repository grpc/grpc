//
//
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
//
//

#ifndef GRPC_TEST_CORE_END2END_FUZZERS_FUZZING_COMMON_H
#define GRPC_TEST_CORE_END2END_FUZZERS_FUZZING_COMMON_H

#include <stdint.h>

#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"

namespace grpc {
namespace testing {

class BasicFuzzer {
 public:
  enum Result { kPending = 0, kComplete = 1, kFailed = 2, kNotSupported = 3 };
  virtual Result ExecuteAction(const api_fuzzer::Action& action);
  virtual ~BasicFuzzer() = default;

 private:
  // Poll any created completion queue to drive the RPC forward.
  virtual Result PollCq() = 0;

  // Channel specific actions.
  // Create an active channel with the specified parameters.
  virtual Result CreateChannel(
      const api_fuzzer::CreateChannel& create_channel) = 0;
  // Check whether the channel is connected and optionally try to connect if it
  // is not connected.
  virtual Result CheckConnectivity(bool try_to_connect) = 0;
  // Watch whether the channel connects within the specified duration.
  virtual Result WatchConnectivity(uint32_t duration_us) = 0;
  // Verify that the channel target can be reliably queried.
  virtual Result ValidateChannelTarget() = 0;
  // Send a http ping on the channel.
  virtual Result SendPingOnChannel() = 0;
  // Close the active channel.
  virtual Result CloseChannel() = 0;

  // Server specific actions
  // Create an active server.
  virtual Result CreateServer(
      const api_fuzzer::CreateServer& create_server) = 0;
  // Request to be notified of a new RPC on the active server.
  virtual Result ServerRequestCall() = 0;
  // Shutdown the active server.
  virtual Result ShutdownServer() = 0;
  // Destroy the active server.
  virtual Result DestroyServerIfReady() = 0;

  // Call specific actions.
  // Create a call on the active channel with the specified parameters. Also add
  // it the list of managed calls.
  virtual Result CreateCall(const api_fuzzer::CreateCall& create_call) = 0;
  // Choose a different active call from the list of managed calls.
  virtual Result ChangeActiveCall() = 0;
  // Queue a batch of operations to be executed on the active call.
  virtual Result QueueBatchForActiveCall(
      const api_fuzzer::Batch& queue_batch) = 0;
  // Cancel the active call.
  virtual Result CancelActiveCall() = 0;
  // Cancel all managed calls.
  virtual Result CancelAllCallsIfShutdown() = 0;
  // Validate that the peer can be reliably queried for the active call.
  virtual Result ValidatePeerForActiveCall() = 0;
  // Cancel and destroy the active call.
  virtual Result DestroyActiveCall() = 0;

  // Other actions.
  // Change the resource quota limits.
  virtual Result ResizeResourceQuota(uint32_t resize_resource_quota) = 0;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CORE_END2END_FUZZERS_FUZZING_COMMON_H
