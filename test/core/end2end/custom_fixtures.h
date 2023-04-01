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

#ifndef GRPC_TEST_CORE_END2END_CUSTOM_FIXTURES_H
#define GRPC_TEST_CORE_END2END_CUSTOM_FIXTURES_H

#include "end2end_tests.h"

#include "test/core/end2end/end2end_tests.h"

// This file is substituted internally to pickup extra fixtures.

namespace grpc_core {

inline std::vector<CoreTestConfiguration> CustomFixtures() {
  return std::vector<CoreTestConfiguration>();
}

}  // namespace grpc_core

#endif
