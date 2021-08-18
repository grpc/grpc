/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_TEST_CPP_UTIL_TEST_CONFIG_H
#define GRPC_TEST_CPP_UTIL_TEST_CONFIG_H

#ifndef GRPC_GTEST_FLAG_SET_DEATH_TEST_STYLE
#define GRPC_GTEST_FLAG_SET_DEATH_TEST_STYLE(style) \
  ::testing::FLAGS_gtest_death_test_style = style
#endif  // GRPC_GTEST_FLAG_SET_DEATH_TEST_STYLE

namespace grpc {
namespace testing {

void InitTest(int* argc, char*** argv, bool remove_flags);

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_TEST_CONFIG_H
