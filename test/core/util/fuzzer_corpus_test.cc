/*
 *
 * Copyright 2016 gRPC authors.
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

#include <stdbool.h>

#include <dirent.h>
#include <gflags/gflags.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <sys/types.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/test_config.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern bool squelch;
extern bool leak_check;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

DEFINE_string(file, "", "Use this file as test data");
DEFINE_string(directory, "", "Use this directory as test data");

class FuzzerCorpusTest : public ::testing::TestWithParam<std::string> {};

TEST_P(FuzzerCorpusTest, RunOneExample) {
  gpr_log(GPR_DEBUG, "Example file: %s", GetParam().c_str());
  grpc_slice buffer;
  squelch = false;
  leak_check = false;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(GetParam().c_str(), 0, &buffer)));
  LLVMFuzzerTestOneInput(GRPC_SLICE_START_PTR(buffer),
                         GRPC_SLICE_LENGTH(buffer));
  grpc_slice_unref(buffer);
}

class ExampleGenerator
    : public ::testing::internal::ParamGeneratorInterface<std::string> {
 public:
  virtual ::testing::internal::ParamIteratorInterface<std::string>* Begin()
      const;
  virtual ::testing::internal::ParamIteratorInterface<std::string>* End() const;

 private:
  void Materialize() const {
    if (examples_.empty()) {
      if (!FLAGS_file.empty()) examples_.push_back(FLAGS_file);
      if (!FLAGS_directory.empty()) {
        char* test_srcdir = gpr_getenv("TEST_SRCDIR");
        if (test_srcdir != nullptr) {
          FLAGS_directory = test_srcdir +
                            std::string("/com_github_grpc_grpc/") +
                            FLAGS_directory;
        }
        DIR* dp;
        struct dirent* ep;
        dp = opendir(FLAGS_directory.c_str());

        if (dp != nullptr) {
          while ((ep = readdir(dp)) != nullptr) {
            if (ep->d_type == DT_REG) {
              examples_.push_back(FLAGS_directory + "/" + ep->d_name);
            }
          }

          (void)closedir(dp);
        } else {
          perror("Couldn't open the directory");
          abort();
        }
        gpr_free(test_srcdir);
      }
    }
  }

  mutable std::vector<std::string> examples_;
};

class ExampleIterator
    : public ::testing::internal::ParamIteratorInterface<std::string> {
 public:
  ExampleIterator(const ExampleGenerator& base_,
                  std::vector<std::string>::const_iterator begin)
      : base_(base_), begin_(begin), current_(begin) {}

  virtual const ExampleGenerator* BaseGenerator() const { return &base_; }

  virtual void Advance() { current_++; }
  virtual ExampleIterator* Clone() const { return new ExampleIterator(*this); }
  virtual const std::string* Current() const { return &*current_; }

  virtual bool Equals(const ParamIteratorInterface<std::string>& other) const {
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

INSTANTIATE_TEST_CASE_P(
    CorpusExamples, FuzzerCorpusTest,
    ::testing::internal::ParamGenerator<std::string>(new ExampleGenerator));

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ParseCommandLineFlags(&argc, &argv, true);
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
