// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_PER_CPU_H
#define GRPC_CORE_LIB_GPRPP_PER_CPU_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <memory>

#include <grpc/support/cpu.h>

#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

template <typename T>
class PerCpu {
 public:
  T& this_cpu() { return data_[ExecCtx::Get()->starting_cpu()]; }

  T* begin() { return data_.get(); }
  T* end() { return data_.get() + cpus_; }
  const T* begin() const { return data_.get(); }
  const T* end() const { return data_.get() + cpus_; }

 private:
  const size_t cpus_ = gpr_cpu_num_cores();
  std::unique_ptr<T[]> data_{new T[cpus_]};
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_PER_CPU_H
