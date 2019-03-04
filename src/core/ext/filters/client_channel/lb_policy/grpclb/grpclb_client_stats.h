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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H

#include <grpc/support/port_platform.h>

#include <grpc/support/atm.h>

#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

class GrpcLbClientStats : public RefCounted<GrpcLbClientStats> {
 public:
  struct DropTokenCount {
    UniquePtr<char> token;
    int64_t count;

    DropTokenCount(UniquePtr<char> token, int64_t count)
        : token(std::move(token)), count(count) {}
  };

  typedef InlinedVector<DropTokenCount, 10> DroppedCallCounts;

  GrpcLbClientStats() {}

  void AddCallStarted();
  void AddCallFinished(bool finished_with_client_failed_to_send,
                       bool finished_known_received);

  // This method is not thread-safe; caller must synchronize.
  void AddCallDroppedLocked(const char* token);

  // This method is not thread-safe; caller must synchronize.
  void GetLocked(int64_t* num_calls_started, int64_t* num_calls_finished,
                 int64_t* num_calls_finished_with_client_failed_to_send,
                 int64_t* num_calls_finished_known_received,
                 UniquePtr<DroppedCallCounts>* drop_token_counts);

  // A destruction function to use as the user_data key when attaching
  // client stats to a grpc_mdelem.
  static void Destroy(void* arg) {
    static_cast<GrpcLbClientStats*>(arg)->Unref();
  }

 private:
  // This field must only be accessed via *_locked() methods.
  UniquePtr<DroppedCallCounts> drop_token_counts_;
  // These fields may be accessed from multiple threads at a time.
  gpr_atm num_calls_started_ = 0;
  gpr_atm num_calls_finished_ = 0;
  gpr_atm num_calls_finished_with_client_failed_to_send_ = 0;
  gpr_atm num_calls_finished_known_received_ = 0;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H \
        */
