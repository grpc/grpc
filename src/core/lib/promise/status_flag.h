// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_STATUS_FLAG_H
#define GRPC_SRC_CORE_LIB_PROMISE_STATUS_FLAG_H

#include "src/core/lib/promise/detail/status.h"

namespace grpc_core {

// A simple flag that looks a little like a status object - enough so that
// one wouldn't confuse it for a bool that represents something else.
class StatusFlag {
 public:
  explicit StatusFlag(bool ok) : ok_(ok) {}

  bool ok() const { return ok_; }

 private:
  bool ok_;
};

inline bool IsStatusOk(StatusFlag flag) { return flag.ok(); }

}  // namespace grpc_core

#endif
