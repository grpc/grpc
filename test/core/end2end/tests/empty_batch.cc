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

CORE_END2END_TEST(CoreEnd2endTest, EmptyBatch) {
  auto c = NewClientCall("/service/method").Create();
  c.NewBatch(1);
  Expect(1, true);
  Step();
}

}  // namespace grpc_core
