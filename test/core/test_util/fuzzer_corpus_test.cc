//
//
// Copyright 2016 gRPC authors.
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

#include <dirent.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/env.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/util/test_config.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern bool squelch;

ABSL_FLAG(std::string, file, "", "Use this file as test data");
ABSL_FLAG(std::string, directory, "", "Use this directory as test data");

class FuzzerCorpusTest : public ::testing::TestWithParam<std::string> {};

TEST_P(FuzzerCorpusTest, RunOneExample) {
  // Need to call grpc_init() here to use a slice, but need to shut it
  // down before calling LLVMFuzzerTestOneInput(), because most
  // implementations of that function will initialize and shutdown gRPC
  // internally.
  fprintf(stderr, "Example file: %s\n", GetParam().c_str());
  squelch = false;
  std::string buffer = grpc_core::testing::GetFileContents(GetParam());
  size_t length = buffer.size();
  void* data = gpr_malloc(length);
  if (length > 0) {
    memcpy(data, buffer.data(), length);
  }
  LLVMFuzzerTestOneInput(static_cast<uint8_t*>(data), length);
  gpr_free(data);
}

class ExampleGenerator
    : public ::testing::internal::ParamGeneratorInterface<std::string> {
 public:
  ::testing::internal::ParamIteratorInterface<std::string>* Begin()
      const override;
  ::testing::internal::ParamIteratorInterface<std::string>* End()
      const override;

 private:
  void Materialize() const {
    if (examples_.empty()) {
      if (!absl::GetFlag(FLAGS_file).empty()) {
        examples_.push_back(absl::GetFlag(FLAGS_file));
      }
      if (!absl::GetFlag(FLAGS_directory).empty()) {
        auto test_srcdir = grpc_core::GetEnv("TEST_SRCDIR");
        VLOG(2) << "test_srcdir=\""
                << (test_srcdir.has_value() ? test_srcdir->c_str() : "(null)")
                << "\"";
        std::string directory = absl::GetFlag(FLAGS_directory);
        if (test_srcdir.has_value()) {
          directory =
              *test_srcdir + std::string("/com_github_grpc_grpc/") + directory;
        }
        VLOG(2) << "Using corpus directory: " << directory;
        DIR* dp;
        struct dirent* ep;
        dp = opendir(directory.c_str());

        if (dp != nullptr) {
          while ((ep = readdir(dp)) != nullptr) {
            if (strcmp(ep->d_name, ".") != 0 && strcmp(ep->d_name, "..") != 0) {
              examples_.push_back(directory + "/" + ep->d_name);
            }
          }

          (void)closedir(dp);
        } else {
          perror("Couldn't open the directory");
          abort();
        }
      }
    }
    // Make sure we don't succeed without doing anything, which caused
    // us to be blind to our fuzzers not running for 9 months.
    CHECK(!examples_.empty());
    // Get a consistent ordering of examples so problems don't just show up on
    // CI
    std::sort(examples_.begin(), examples_.end());
  }

  mutable std::vector<std::string> examples_;
};

class ExampleIterator
    : public ::testing::internal::ParamIteratorInterface<std::string> {
 public:
  ExampleIterator(const ExampleGenerator& base_,
                  std::vector<std::string>::const_iterator begin)
      : base_(base_), begin_(begin), current_(begin) {}

  const ExampleGenerator* BaseGenerator() const override { return &base_; }

  void Advance() override { current_++; }
  ExampleIterator* Clone() const override { return new ExampleIterator(*this); }
  const std::string* Current() const override { return &*current_; }

  bool Equals(const ParamIteratorInterface<std::string>& other) const override {
    return &base_ == other.BaseGenerator() &&
           current_ == dynamic_cast<const ExampleIterator*>(&other)->current_;
  }

 private:
  ExampleIterator(const ExampleIterator& other)
      : base_(other.base_), begin_(other.begin_), current_(other.current_) {}

  const ExampleGenerator& base_;
  const std::vector<std::string>::const_iterator begin_;
  std::vector<std::string>::const_iterator current_;
};

::testing::internal::ParamIteratorInterface<std::string>*
ExampleGenerator::Begin() const {
  Materialize();
  return new ExampleIterator(*this, examples_.begin());
}

::testing::internal::ParamIteratorInterface<std::string>*
ExampleGenerator::End() const {
  Materialize();
  return new ExampleIterator(*this, examples_.end());
}

INSTANTIATE_TEST_SUITE_P(
    CorpusExamples, FuzzerCorpusTest,
    ::testing::internal::ParamGenerator<std::string>(new ExampleGenerator));

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
