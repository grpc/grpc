// Copyright 2021 gRPC authors.
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

#ifndef LOCAL_GOOGLE_HOME_CTILLER_GRPC_TEST_CORE_RESOURCE_QUOTA_CALL_CHECKER_H_
#define LOCAL_GOOGLE_HOME_CTILLER_GRPC_TEST_CORE_RESOURCE_QUOTA_CALL_CHECKER_H_

#include <memory>

#include <grpc/support/log.h>

namespace grpc_core {
namespace testing {

// Utility to help check a function is called.
// Usage:
// auto checker = CallChecker::Make();
// auto f = [checker]() {
//   checker.Called();
// };
// Will crash if: f never called, or f called more than once.
class CallChecker {
 public:
  ~CallChecker() { GPR_ASSERT(called_); }

  void Called() {
    GPR_ASSERT(!called_);
    called_ = true;
  }

  static std::shared_ptr<CallChecker> Make() {
    return std::make_shared<CallChecker>();
  }

 private:
  bool called_ = false;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // LOCAL_GOOGLE_HOME_CTILLER_GRPC_TEST_CORE_RESOURCE_QUOTA_CALL_CHECKER_H_
