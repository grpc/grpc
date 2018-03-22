/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include <set>
#include <vector>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/cpp/server/load_reporter/load_data_store.h"
#include "src/cpp/server/load_reporter/load_reporter.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

// TODO(juanlishen): Add tests when Census confirms proto.

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
