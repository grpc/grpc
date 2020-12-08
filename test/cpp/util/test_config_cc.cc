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

#include <vector>

#include "absl/flags/parse.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

void InitTest(int* argc, char*** argv, bool remove_flags) {
  std::vector<char*> reduced_argv = absl::ParseCommandLine(*argc, *argv);
  if (remove_flags) {
    *argc = reduced_argv.size();
    for (int i = 0; i < *argc; i++) {
      (*argv)[i] = reduced_argv.at(i);
    }
  }
}

}  // namespace testing
}  // namespace grpc
