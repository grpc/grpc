// Copyright 2025 gRPC authors.
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

#include "boringssl.h"

#include <fstream>
#include <initializer_list>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

void AddBoringSslMetadata(nlohmann::json& metadata) {
  std::ifstream sources_in(
      "../../third_party/boringssl-with-bazel/sources.json");
  auto sources = nlohmann::json::parse(sources_in);
  auto file_list = [&sources](std::initializer_list<std::string> sections) {
    std::vector<std::string> ret;
    for (const auto& section : sections) {
      const auto& files = sources[section];
      for (const auto& file : files) {
        std::string file_str = file;
        ret.push_back(
            absl::StrCat("third_party/boringssl-with-bazel/", file_str));
      }
    }
    std::sort(ret.begin(), ret.end());
    return ret;
  };
  std::vector<std::string> asm_outputs;
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    for (const auto& file : it.value()) {
      std::string file_str = file;
      if (absl::EndsWith(file_str, ".S") || absl::EndsWith(file_str, ".asm")) {
        asm_outputs.push_back(file);
      }
    }
  }
  metadata["raw_boringssl_build_output_for_debugging"]["files"] = sources;
  metadata["libs"].push_back(
      {{"name", "boringssl"},
       {"build", "private"},
       {"language", "c"},
       {"secure", false},
       {"src", file_list({"ssl", "crypto"})},
       {"asm_src", file_list({"asm"})},
       {"headers",
        file_list({"ssl_headers", "ssl_internal_headers", "crypto_headers",
                   "crypto_internal_headers", "fips_fragments"})},
       {"boringssl", true},
       {"defaults", "boringssl"}});
  metadata["libs"].push_back({{"name", "boringssl_test_util"},
                              {"build", "private"},
                              {"language", "c++"},
                              {"secure", false},
                              {"boringssl", true},
                              {"defaults", "boringssl"},
                              {"src", file_list({"test_support"})}});
  for (const auto& test : {"ssl_test", "crypto_test"}) {
    metadata["targets"].push_back(
        {{"name", absl::StrCat("boringssl_", test)},
         {"build", "test"},
         {"run", false},
         {"secure", false},
         {"language", "c++"},
         {"src", file_list({test})},
         {"boringssl", true},
         {"defaults", "boringssl"},
         {"deps", {"boringssl_test_util", "boringssl"}}});
    metadata["tests"].push_back({
        {"name", absl::StrCat("boringssl_", test)},
        {"args", {}},
        {"exclude_configs", {"asan", "ubsan"}},
        {"ci_platforms", {"linux", "mac", "posix", "windows"}},
        {"platforms", {"linux", "mac", "posix", "windows"}},
        {"flaky", false},
        {"gtest", true},
        {"language", "c++"},
        {"boringssl", true},
        {"defaults", "boringssl"},
    });
  }
}
