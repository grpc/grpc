//
//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_TEST_CORE_UTIL_PASSTHRU_ENDPOINT_H
#define GRPC_TEST_CORE_UTIL_PASSTHRU_ENDPOINT_H

#include <stdint.h>

#include <vector>

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/endpoint.h"

// The struct is refcounted, always use grpc_passthru_endpoint_stats_create and
// grpc_passthru_endpoint_stats_destroy, rather then embedding it in your
// objects by value.
typedef struct {
  gpr_refcount refs;
  gpr_atm num_writes;
} grpc_passthru_endpoint_stats;

typedef struct {
  uint64_t wait_ms;
  uint64_t add_n_writable_bytes;
  uint64_t add_n_readable_bytes;
} grpc_passthru_endpoint_channel_action;

void grpc_passthru_endpoint_create(grpc_endpoint** client,
                                   grpc_endpoint** server,
                                   grpc_passthru_endpoint_stats* stats,
                                   bool simulate_channel_actions = false);

grpc_passthru_endpoint_stats* grpc_passthru_endpoint_stats_create();

void grpc_passthru_endpoint_stats_destroy(grpc_passthru_endpoint_stats* stats);

void start_scheduling_grpc_passthru_endpoint_channel_effects(
    grpc_endpoint* ep,
    const std::vector<grpc_passthru_endpoint_channel_action>& actions);

#endif  // GRPC_TEST_CORE_UTIL_PASSTHRU_ENDPOINT_H
