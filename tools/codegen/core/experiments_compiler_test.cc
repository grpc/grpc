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

#include "tools/codegen/core/experiments_compiler.h"

#include <fstream>
#include <map>
#include <string>

#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc {
namespace testing {

class ExperimentsCompilerTest : public ::testing::Test {
 public:
  ExperimentsCompilerTest()
      : compiler_(allowed_defaults_, allowed_platforms_, final_return_,
                  final_define_, bzl_list_for_defaults_) {}
  absl::Status AddExperimentDefinition(std::string experiments_yaml_content) {
    return compiler_.AddExperimentDefinition(experiments_yaml_content);
  }
  absl::Status AddRolloutSpecification(
      std::string experiments_rollout_yaml_content) {
    return compiler_.AddRolloutSpecification(experiments_rollout_yaml_content);
  }
  absl::Status GenerateExperimentsHdr(const std::string& output_file,
                                      const std::string& mode) {
    if (mode == "production" || mode == "test") {
      grpc_core::GrpcOssExperimentsOutputGenerator generator(compiler_, mode);
      return compiler_.GenerateExperimentsHdr(output_file, generator);
    } else {
      return absl::InternalError("Unsupported mode: " + mode);
    }
  }

  absl::Status GenerateExperimentsSrc(const std::string& output_file,
                                      const std::string& header_file_path,
                                      const std::string& mode) {
    if (mode == "production" || mode == "test") {
      grpc_core::GrpcOssExperimentsOutputGenerator generator(compiler_, mode,
                                                             header_file_path);
      return compiler_.GenerateExperimentsSrc(output_file, header_file_path,
                                              generator);
    } else {
      return absl::InternalError("Unsupported mode: " + mode);
    }
  }

  absl::Status ReadFile(std::string filename, std::string& output) {
    std::ifstream infile(filename);  // Open the file for reading
    if (infile.is_open()) {
      std::string line;
      // Read the file line by line (common approach)
      while (std::getline(infile, line)) {
        output += line + "\n";
      }
      infile.close();
    } else {
      return absl::InternalError("Failed to open file: " + filename);
    }
    return absl::OkStatus();
  }

 private:
  std::map<std::string, std::string> allowed_defaults_ = {
      {"broken", "false"},
      {"False", "false"},
      {"True", "true"},
      {"debug", "kDefaultForDebugOnly"}};
  std::map<std::string, std::string> allowed_platforms_ = {
      {"windows", "GPR_WINDOWS"}, {"ios", "GRPC_CFSTREAM"}, {"posix", ""}};
  std::map<std::string, std::string> final_return_ = {
      {"broken", "return false;"},
      {"False", "return false;"},
      {"True", "return true;"},
      {"debug",
       "\n#ifdef NDEBUG\nreturn false;\n#else\nreturn true;\n#endif\n"},
  };
  std::map<std::string, std::string> final_define_ = {
      {"broken", ""},
      {"False", ""},
      {"True", "#define %s"},
      {"debug", "#ifndef NDEBUG\n#define %s\n#endif"},
  };
  std::map<std::string, std::string> bzl_list_for_defaults_ = {
      {"broken", ""},
      {"False", "off"},
      {"True", "on"},
      {"debug", "dbg"},
  };
  grpc_core::ExperimentsCompiler compiler_;
};

TEST_F(ExperimentsCompilerTest, GenerateGrpcOssProductionExperimentsOutput) {
  EXPECT_OK(AddExperimentDefinition(
      "name: test_experiment\ndescription: test experiment\nowner: "
      "ladynana\nexpiry: 2025-01-01\nuses_polling: true\nallow_in_fuzzing_"
      "config: true\ntest_tags: [\"test_tag_1\", \"test_tag_2\"]\n"));
  EXPECT_OK(
      AddRolloutSpecification("name: test_experiment\ndefault_value: True\n"));
  // Check the experiment definitions and rollout specifications are added
  // correctly.
  std::string hdr_filename = "/tmp/experiments.github.h";
  std::string src_filename = "/tmp/experiments.github.cc";
  EXPECT_OK(GenerateExperimentsHdr(hdr_filename, "production"));
  EXPECT_OK(GenerateExperimentsSrc(src_filename, hdr_filename, "production"));
  std::string hdr_output;
  std::string src_output;
  EXPECT_OK(ReadFile(hdr_filename, hdr_output));
  EXPECT_OK(ReadFile(src_filename, src_output));
  // Check output file is generated correctly.
  std::string expected_hdr_output =
      grpc_core::GetCopyright() +
      "// Auto generated by "
      "tools/codegen/core/gen_experiments_grpc_oss.cc\n" +
      R"(// This file contains the autogenerated parts of the experiments API.
//
// It generates two symbols for each experiment.
//
// For the experiment named new_car_project, it generates:
//
// - a function IsNewCarProjectEnabled() that returns true if the experiment
//   should be enabled at runtime.
//
// - a macro GRPC_EXPERIMENT_IS_INCLUDED_NEW_CAR_PROJECT that is defined if the
//   experiment *could* be enabled at runtime.
//
// The function is used to determine whether to run the experiment or
// non-experiment code path.
//
// If the experiment brings significant bloat, the macro can be used to avoid
// including the experiment code path in the binary for binaries that are size
// sensitive.
//
// By default that includes our iOS and Android builds.
//
// Finally, a small array is included that contains the metadata for each
// experiment.
//
// A macro, GRPC_EXPERIMENTS_ARE_FINAL, controls whether we fix experiment
// configuration at build time (if it's defined) or allow it to be tuned at
// runtime (if it's disabled).
//
// If you are using the Bazel build system, that macro can be configured with
// --define=grpc_experiments_are_final=true.

#ifndef GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
#define GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/config.h"

namespace grpc_core {

#ifdef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return true; }
elif defined(GPR_WINDOWS)
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return true; }
#else
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return true; }
#endif

#else
 enum ExperimentIds {
  kExperimentId{TestExperiment,
  {kNumExperiments}
};
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return IsExperimentEnabled<kExperimentIdTestExperiment>(); }
extern const ExperimentMetadatag_experiment_metadata[kNumExperiments];
#endif
}  // namespace grpc_core
#endif  // GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
)";
  std::string expected_src_output =
      grpc_core::GetCopyright() +
      "// Auto generated by "
      "tools/codegen/core/gen_experiments_grpc_oss.cc\n" +
      R"(
#include <grpc/support/port_platform.h>

#include "/tmp/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL
#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_test_experiment = "test experiment";
const char* const additional_constraints_test_experiment = "{}";
}

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
  {"test_experiment", description_test_experiment, additional_constraints_test_experiment, nullptr, 0, true, true},};

}  // namespace grpc_core

#elif defined(GPR_WINDOWS)
namespace {
const char* const description_test_experiment = "test experiment";
const char* const additional_constraints_test_experiment = "{}";
}

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
  {"test_experiment", description_test_experiment, additional_constraints_test_experiment, nullptr, 0, true, true},};

}  // namespace grpc_core

#else
namespace {
const char* const description_test_experiment = "test experiment";
const char* const additional_constraints_test_experiment = "{}";
}

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
  {"test_experiment", description_test_experiment, additional_constraints_test_experiment, nullptr, 0, true, true},};

}  // namespace grpc_core
#endif
#endif
)";
  EXPECT_EQ(expected_hdr_output, hdr_output);
  EXPECT_EQ(expected_src_output, src_output);
}

TEST_F(ExperimentsCompilerTest, GenerateGrpcOssTestExperimentsOutput) {
  EXPECT_OK(AddExperimentDefinition(
      "name: test_experiment\ndescription: test experiment\nowner: "
      "ladynana\nexpiry: 2025-01-01\nuses_polling: true\nallow_in_fuzzing_"
      "config: true\ntest_tags: [\"test_tag_1\", \"test_tag_2\"]\n"));
  EXPECT_OK(
      AddRolloutSpecification("name: test_experiment\ndefault_value: True\n"));
  // Check the experiment definitions and rollout specifications are added
  // correctly.
  std::string hdr_filename = "/tmp/experiments.github.h";
  std::string src_filename = "/tmp/experiments.github.cc";
  EXPECT_OK(GenerateExperimentsHdr(hdr_filename, "test"));
  EXPECT_OK(GenerateExperimentsSrc(src_filename, hdr_filename, "test"));
  std::string hdr_output;
  std::string src_output;
  EXPECT_OK(ReadFile(hdr_filename, hdr_output));
  EXPECT_OK(ReadFile(src_filename, src_output));
  // Check output file is generated correctly.
  std::string expected_hdr_output =
      grpc_core::GetCopyright() +
      "// Auto generated by "
      "tools/codegen/core/gen_experiments_grpc_oss.cc\n" +
      R"(// This file contains the autogenerated parts of the experiments API.
//
// It generates two symbols for each experiment.
//
// For the experiment named new_car_project, it generates:
//
// - a function IsNewCarProjectEnabled() that returns true if the experiment
//   should be enabled at runtime.
//
// - a macro GRPC_EXPERIMENT_IS_INCLUDED_NEW_CAR_PROJECT that is defined if the
//   experiment *could* be enabled at runtime.
//
// The function is used to determine whether to run the experiment or
// non-experiment code path.
//
// If the experiment brings significant bloat, the macro can be used to avoid
// including the experiment code path in the binary for binaries that are size
// sensitive.
//
// By default that includes our iOS and Android builds.
//
// Finally, a small array is included that contains the metadata for each
// experiment.
//
// A macro, GRPC_EXPERIMENTS_ARE_FINAL, controls whether we fix experiment
// configuration at build time (if it's defined) or allow it to be tuned at
// runtime (if it's disabled).
//
// If you are using the Bazel build system, that macro can be configured with
// --define=grpc_experiments_are_final=true.

#ifndef GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
#define GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/config.h"

namespace grpc_core {

#ifdef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return true; }
elif defined(GPR_WINDOWS)
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return true; }
#else
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return true; }
#endif

#else
 enum ExperimentIds {
  kExperimentId{TestExperiment,
  {kNumExperiments}
};
#define GRPC_EXPERIMENT_IS_INCLUDED_TEST_EXPERIMENT
inline bool IsTestExperimentEnabled() { return IsExperimentEnabled<kExperimentIdTestExperiment>(); }
extern const ExperimentMetadatag_experiment_metadata[kNumExperiments];
#endif
}  // namespace grpc_core
#endif  // GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
)";
  std::string expected_src_output =
      grpc_core::GetCopyright() +
      "// Auto generated by "
      "tools/codegen/core/gen_experiments_grpc_oss.cc\n" +
      R"(
#include <grpc/support/port_platform.h>

#include "/tmp/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL
#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_test_experiment = "test experiment";
const char* const additional_constraints_test_experiment = "{}";
}

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
  {"test_experiment", description_test_experiment, additional_constraints_test_experiment, nullptr, 0, true, true},};

}  // namespace grpc_core

#elif defined(GPR_WINDOWS)
namespace {
const char* const description_test_experiment = "test experiment";
const char* const additional_constraints_test_experiment = "{}";
}

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
  {"test_experiment", description_test_experiment, additional_constraints_test_experiment, nullptr, 0, true, true},};

}  // namespace grpc_core

#else
namespace {
const char* const description_test_experiment = "test experiment";
const char* const additional_constraints_test_experiment = "{}";
}

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
  {"test_experiment", description_test_experiment, additional_constraints_test_experiment, nullptr, 0, true, true},};

}  // namespace grpc_core
#endif
#endif
)";
  EXPECT_EQ(expected_hdr_output, hdr_output);
  EXPECT_EQ(expected_src_output, src_output);
}
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
