/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_LIB_SUPPORT_ATOMIC_WITH_ATM_H
#define GRPC_CORE_LIB_SUPPORT_ATOMIC_WITH_ATM_H

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

#endif /* GRPC_CORE_LIB_SUPPORT_ATOMIC_WITH_ATM_H */
