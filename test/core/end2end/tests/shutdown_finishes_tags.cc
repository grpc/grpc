//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreEnd2endTest, ShutdownFinishesTags) {
  // upon shutdown, the server should finish all requested calls indicating
  // no new call
  auto s = RequestCall(101);
  ShutdownAndDestroyClient();
  ShutdownServerAndNotify(1000);
  Expect(101, false);
  Expect(1000, true);
  Step();
}

}  // namespace
}  // namespace grpc_core
