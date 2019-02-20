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

enum class MemoryOrder {
  RELAXED = std::memory_order_relaxed,
  CONSUME = std::memory_order_consume,
  ACQUIRE = std::memory_order_acquire,
  RELEASE = std::memory_order_release,
  ACQ_REL = std::memory_order_acq_rel,
  SEQ_CST = std::memory_order_seq_cst
};

// Prefer the helper methods below over the same functions provided by
// std::atomic, because they maintain stats over atomic opertions which are
// useful for comparing benchmarks.

template <typename T>
T AtomicLoad(const Atomic<T>* storage, MemoryOrder order) {
  return storage->load(static_cast<std::memory_order>(order));
}

template <typename T>
T AtomicStore(Atomic<T>* storage, T val, MemoryOrder order) {
  return storage->store(val, static_cast<std::memory_order>(order));
}
template <typename T>
bool AtomicCompareExchangeWeak(Atomic<T>* storage, T* expected, T desired,
                               MemoryOrder success, MemoryOrder failure) {
  return GPR_ATM_INC_CAS_THEN(
      storage->compare_exchange_weak(*expected, desired, success, failure));
}

template <typename T>
bool AtomicCompareExchangeStrong(Atomic<T>* storage, T* expected, T desired,
                                 MemoryOrder success, MemoryOrder failure) {
  return GPR_ATM_INC_CAS_THEN(storage->compare_exchange_weak(
      *expected, desired, static_cast<std::memory_order>(success),
      static_cast<std::memory_order>(failure)));
}

template <typename T, typename Arg>
T AtomicFetchAdd(Atomic<T>* storage, Arg arg,
                 MemoryOrder order = MemoryOrder::SEQ_CST) {
  return GPR_ATM_INC_ADD_THEN(storage->fetch_add(
      static_cast<Arg>(arg), static_cast<std::memory_order>(order)));
}

template <typename T, typename Arg>
T AtomicFetchSub(Atomic<T>* storage, Arg arg,
                 MemoryOrder order = MemoryOrder::SEQ_CST) {
  return GPR_ATM_INC_ADD_THEN(storage->fetch_sub(
      static_cast<Arg>(arg), static_cast<std::memory_order>(order)));
}

// Atomically increment a counter only if the counter value is not zero.
// Returns true if increment took place; false if counter is zero.
template <class T>
bool AtomicIncrementIfNonzero(Atomic<T>* counter,
                              MemoryOrder load_order = MemoryOrder::ACQ_REL) {
  T count = counter->load(static_cast<std::memory_order>(load_order));
  do {
    // If zero, we are done (without an increment). If not, we must do a CAS to
    // maintain the contract: do not increment the counter if it is already zero
    if (count == 0) {
      return false;
    }
  } while (!AtomicCompareExchangeWeak(
      counter, &count, count + 1,
      static_cast<std::memory_order>(MemoryOrder::ACQ_REL),
      static_cast<std::memory_order>(load_order)));
  return true;
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_ATOMIC_H */
