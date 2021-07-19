/*
 *
 * Copyright 2021 gRPC authors.
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

#include "src/core/ext/xds/xds_circuit_breaker_retry_map.h"

namespace grpc_core {
XdsCircuitBreakerRetryMap* g_retry_map = nullptr;

RefCountedPtr<XdsCircuitBreakerRetryMap::RetryCounter>
XdsCircuitBreakerRetryMap::GetOrCreate(const std::string& cluster) {
  Key key(cluster);
  RefCountedPtr<RetryCounter> result;
  MutexLock lock(&g_retry_map->mu_);
  auto it = g_retry_map->map_.find(key);
  if (it == g_retry_map->map_.end()) {
    it = g_retry_map->map_.insert({key, nullptr}).first;
  } else {
    result = it->second->RefIfNonZero();
  }
  if (result == nullptr) {
    result = MakeRefCounted<RetryCounter>(std::move(key));
    it->second = result.get();
  }
  return result;
}

XdsCircuitBreakerRetryMap::RetryCounter::~RetryCounter() {
  MutexLock lock(&g_retry_map->mu_);
  auto it = g_retry_map->map_.find(key_);
  if (it != g_retry_map->map_.end() && it->second == this) {
    g_retry_map->map_.erase(it);
  }
}

void XdsCircuitBreakerRetryMap::Init() {
  g_retry_map = new XdsCircuitBreakerRetryMap();
}

void XdsCircuitBreakerRetryMap::Shutdown() { delete g_retry_map; }

}  // namespace grpc_core
