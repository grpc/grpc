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

#ifndef GRPC_CORE_LIB_GPRPP_ATOMIC_WITH_ATM_H
#define GRPC_CORE_LIB_GPRPP_ATOMIC_WITH_ATM_H

#include <grpc/support/atm.h>

namespace grpc_core {

enum MemoryOrderRelaxed { memory_order_relaxed };

template <class T>
class atomic;

template <>
class atomic<bool> {
 public:
  atomic() { gpr_atm_no_barrier_store(&x_, static_cast<gpr_atm>(false)); }
  explicit atomic(bool x) {
    gpr_atm_no_barrier_store(&x_, static_cast<gpr_atm>(x));
  }

  bool compare_exchange_strong(bool& expected, bool update, MemoryOrderRelaxed,
                               MemoryOrderRelaxed) {
    if (!gpr_atm_no_barrier_cas(&x_, static_cast<gpr_atm>(expected),
                                static_cast<gpr_atm>(update))) {
      expected = gpr_atm_no_barrier_load(&x_) != 0;
      return false;
    }
    return true;
  }

 private:
  gpr_atm x_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_ATOMIC_WITH_ATM_H */
