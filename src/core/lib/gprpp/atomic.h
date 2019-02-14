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

#ifndef GRPC_CORE_LIB_GPRPP_ATOMIC_H
#define GRPC_CORE_LIB_GPRPP_ATOMIC_H

#include <grpc/support/port_platform.h>

#include <atomic>

namespace grpc_core {

template <typename T>
using Atomic = std::atomic<T>;

// Prefer the helper methods below over the same functions provided by
// std::atomic, because they maintain stats over atomic opertions which are
// useful for comparing benchmarks.

template <typename T>
bool AtomicCompareExchangeWeak(std::atomic<T>* storage, T* expected, T desired,
                               std::memory_order success,
                               std::memory_order failure) {
  return GPR_ATM_INC_CAS_THEN(
      storage->compare_exchange_weak(*expected, desired, success, failure));
}

template <typename T>
bool AtomicCompareExchangeStrong(std::atomic<T>* storage, T* expected,
                                 T desired, std::memory_order success,
                                 std::memory_order failure) {
  return GPR_ATM_INC_CAS_THEN(
      storage->compare_exchange_weak(*expected, desired, success, failure));
}

template <typename T, typename Arg>
T AtomicFetchAdd(std::atomic<T>* storage, Arg arg,
                 std::memory_order order = std::memory_order_seq_cst) {
  return GPR_ATM_INC_ADD_THEN(storage->fetch_add(static_cast<Arg>(arg), order));
}

template <typename T, typename Arg>
T AtomicFetchSub(std::atomic<T>* storage, Arg arg,
                 std::memory_order order = std::memory_order_seq_cst) {
  return GPR_ATM_INC_ADD_THEN(storage->fetch_sub(static_cast<Arg>(arg), order));
}

// Atomically increment a counter only if the counter value is not zero.
// Returns true if increment took place; false if counter is zero.
template <class T>
bool AtomicIncrementIfNonzero(
    std::atomic<T>* counter,
    std::memory_order load_order = std::memory_order_acquire) {
  T count = counter->load(load_order);
  do {
    // If zero, we are done (without an increment). If not, we must do a CAS to
    // maintain the contract: do not increment the counter if it is already zero
    if (count == 0) {
      return false;
    }
  } while (!AtomicCompareExchangeWeak(counter, &count, count + 1,
                                      std::memory_order_acq_rel, load_order));
  return true;
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_ATOMIC_H */
