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

#include <stdio.h>

#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto all_tests = grpc_core::CoreEnd2endTestRegistry::Get().AllTests();
  for (const auto& test : all_tests) {
    auto text =
        absl::StrCat("suite: \"", test.suite, "\"\n", "test: \"", test.name,
                     "\"\n", "config: \"", test.config->name, "\"\n");
    auto file =
        absl::StrCat("test/core/end2end/end2end_test_corpus/", test.suite, "_",
                     test.name, "_", test.config->name, ".textproto");
    fprintf(stderr, "WRITE: %s\n", file.c_str());
    FILE* f = fopen(file.c_str(), "w");
    if (!f) return 1;
    auto cleanup = absl::MakeCleanup([f]() { fclose(f); });
    fwrite(text.c_str(), 1, text.size(), f);
  }
}
