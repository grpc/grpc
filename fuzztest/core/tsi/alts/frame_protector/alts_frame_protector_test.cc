// Copyright 2024 The gRPC Authors.
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

#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace grpc_core {

void AltsTestDoRoundTrip() {}
FUZZ_TEST(MyTestSuite, AltsTestDoRoundTrip);

}  // namespace grpc_core
