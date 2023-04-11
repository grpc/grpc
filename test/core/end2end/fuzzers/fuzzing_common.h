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

#ifndef TEST_CORE_END2END_FUZZERS_FUZZING_COMMON_H_
#define TEST_CORE_END2END_FUZZERS_FUZZING_COMMON_H_

#include <stdint.h>

#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"

namespace grpc {
namespace testing {

class BasicApiFuzzer {
 public:
  enum Result { kPending = 0, kComplete = 1, kFailed = 2, kNotSupported = 3 };
  virtual Result ExecuteAction(const api_fuzzer::Action& action);
  virtual ~BasicApiFuzzer() = default;

 private:
  virtual Result PollCq() = 0;
  // Channel specific actions
  virtual Result CreateChannel(
      const api_fuzzer::CreateChannel& create_channel) = 0;
  virtual Result CheckConnectivity(bool try_to_connect) = 0;
  virtual Result WatchConnectivity(uint32_t duration_us) = 0;
  virtual Result ValidateChannelTarget() = 0;
  virtual Result SendPingOnChannel() = 0;
  virtual Result CloseChannel() = 0;
  // Server specific actions
  virtual Result CreateServer(
      const api_fuzzer::CreateServer& create_server) = 0;
  virtual Result ServerRequestCall() = 0;
  virtual Result ShutdownServer() = 0;
  virtual Result DestroyServerIfReady() = 0;
  // Call specific actions
  virtual Result CreateCall(const api_fuzzer::CreateCall& create_call) = 0;
  virtual Result ChangeActiveCall() = 0;
  virtual Result QueueBatchForActiveCall(
      const api_fuzzer::Batch& queue_batch) = 0;
  virtual Result CancelActiveCall() = 0;
  virtual Result CancelAllCallsIfShutdown() = 0;
  virtual Result ValidatePeerForActiveCall() = 0;
  virtual Result DestroyActiveCall() = 0;
  // Other actions
  virtual Result ResizeResourceQuota(uint32_t resize_resource_quota) = 0;
};

}  // namespace testing
}  // namespace grpc

#endif  // TEST_CORE_END2END_FUZZERS_FUZZING_COMMON_H_
