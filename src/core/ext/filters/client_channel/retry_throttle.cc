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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/retry_throttle.h"

#include <limits.h>
#include <string.h>

#include <map>
#include <string>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/manual_constructor.h"

namespace grpc_core {
namespace internal {

//
// ServerRetryThrottleData
//

ServerRetryThrottleData::ServerRetryThrottleData(
    intptr_t max_milli_tokens, intptr_t milli_token_ratio,
    ServerRetryThrottleData* old_throttle_data)
    : max_milli_tokens_(max_milli_tokens),
      milli_token_ratio_(milli_token_ratio) {
  intptr_t initial_milli_tokens = max_milli_tokens;
  // If there was a pre-existing entry for this server name, initialize
  // the token count by scaling proportionately to the old data.  This
  // ensures that if we're already throttling retries on the old scale,
  // we will start out doing the same thing on the new one.
  if (old_throttle_data != nullptr) {
    double token_fraction =
        static_cast<intptr_t>(
            gpr_atm_acq_load(&old_throttle_data->milli_tokens_)) /
        static_cast<double>(old_throttle_data->max_milli_tokens_);
    initial_milli_tokens =
        static_cast<intptr_t>(token_fraction * max_milli_tokens);
  }
  gpr_atm_rel_store(&milli_tokens_, static_cast<gpr_atm>(initial_milli_tokens));
  // If there was a pre-existing entry, mark it as stale and give it a
  // pointer to the new entry, which is its replacement.
  if (old_throttle_data != nullptr) {
    Ref().release();  // Ref held by pre-existing entry.
    gpr_atm_rel_store(&old_throttle_data->replacement_,
                      reinterpret_cast<gpr_atm>(this));
  }
}

ServerRetryThrottleData::~ServerRetryThrottleData() {
  ServerRetryThrottleData* replacement =
      reinterpret_cast<ServerRetryThrottleData*>(
          gpr_atm_acq_load(&replacement_));
  if (replacement != nullptr) {
    replacement->Unref();
  }
}

void ServerRetryThrottleData::GetReplacementThrottleDataIfNeeded(
    ServerRetryThrottleData** throttle_data) {
  while (true) {
    ServerRetryThrottleData* new_throttle_data =
        reinterpret_cast<ServerRetryThrottleData*>(
            gpr_atm_acq_load(&(*throttle_data)->replacement_));
    if (new_throttle_data == nullptr) return;
    *throttle_data = new_throttle_data;
  }
}

bool ServerRetryThrottleData::RecordFailure() {
  // First, check if we are stale and need to be replaced.
  ServerRetryThrottleData* throttle_data = this;
  GetReplacementThrottleDataIfNeeded(&throttle_data);
  // We decrement milli_tokens by 1000 (1 token) for each failure.
  const intptr_t new_value =
      static_cast<intptr_t>(gpr_atm_no_barrier_clamped_add(
          &throttle_data->milli_tokens_, static_cast<gpr_atm>(-1000),
          static_cast<gpr_atm>(0),
          static_cast<gpr_atm>(throttle_data->max_milli_tokens_)));
  // Retries are allowed as long as the new value is above the threshold
  // (max_milli_tokens / 2).
  return new_value > throttle_data->max_milli_tokens_ / 2;
}

void ServerRetryThrottleData::RecordSuccess() {
  // First, check if we are stale and need to be replaced.
  ServerRetryThrottleData* throttle_data = this;
  GetReplacementThrottleDataIfNeeded(&throttle_data);
  // We increment milli_tokens by milli_token_ratio for each success.
  gpr_atm_no_barrier_clamped_add(
      &throttle_data->milli_tokens_,
      static_cast<gpr_atm>(throttle_data->milli_token_ratio_),
      static_cast<gpr_atm>(0),
      static_cast<gpr_atm>(throttle_data->max_milli_tokens_));
}

//
// ServerRetryThrottleMap
//

using StringToDataMap =
    std::map<std::string, RefCountedPtr<ServerRetryThrottleData>>;
static gpr_mu g_mu;
static StringToDataMap* g_map;

void ServerRetryThrottleMap::Init() {
  gpr_mu_init(&g_mu);
  g_map = new StringToDataMap();
}

void ServerRetryThrottleMap::Shutdown() {
  gpr_mu_destroy(&g_mu);
  delete g_map;
  g_map = nullptr;
}

RefCountedPtr<ServerRetryThrottleData> ServerRetryThrottleMap::GetDataForServer(
    const std::string& server_name, intptr_t max_milli_tokens,
    intptr_t milli_token_ratio) {
  RefCountedPtr<ServerRetryThrottleData> result;
  gpr_mu_lock(&g_mu);
  auto it = g_map->find(server_name);
  ServerRetryThrottleData* throttle_data =
      it == g_map->end() ? nullptr : it->second.get();
  if (throttle_data == nullptr ||
      throttle_data->max_milli_tokens() != max_milli_tokens ||
      throttle_data->milli_token_ratio() != milli_token_ratio) {
    // Entry not found, or found with old parameters.  Create a new one.
    it = g_map
             ->emplace(server_name,
                       MakeRefCounted<ServerRetryThrottleData>(
                           max_milli_tokens, milli_token_ratio, throttle_data))
             .first;
    throttle_data = it->second.get();
  }
  gpr_mu_unlock(&g_mu);
  return throttle_data->Ref();
}

}  // namespace internal
}  // namespace grpc_core
