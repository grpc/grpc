/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_COMBINER_H
#define GRPC_CORE_LIB_IOMGR_COMBINER_H

#include <stddef.h>

#include <grpc/support/atm.h>
#include <vector>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/mpscq.h"

namespace grpc_core {

class Combiner {
 private:
  class Callback {
   public:
    virtual void Run() = 0;
  };

 public:
  template <class F>
  void Run(F f) {
    if (IsActiveOnThisThread()) {
      f();
    } else {
      Schedule(f);
    }
  }

  template <class F>
  void Schedule(F f) {}

  template <class F>
  void Finally(F f) {
    auto c = MakeCallback(f);
    Run([this, c]() { finally_.push_back(c); });
  }

 private:
  bool IsActiveOnThisThread();

  template <class F>
  Callback* MakeCallback(F f);

  std::vector<Callback*> finally_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_COMBINER_H */
