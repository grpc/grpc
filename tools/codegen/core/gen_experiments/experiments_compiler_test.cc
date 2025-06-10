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

#include "tools/codegen/core/gen_experiments/experiments_compiler.h"

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
  absl::Status AddExperimentDefinition() {
    return compiler_.AddExperimentDefinition(experiments_defs_content_);
  }
  absl::Status AddExperimentDefinitionWithCircularDependency() {
    return compiler_.AddExperimentDefinition(
        experiments_defs_content_with_circular_dependency_);
  }
  absl::Status AddRolloutSpecification() {
    return compiler_.AddRolloutSpecification(experiments_rollout_yaml_content_);
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
      {"false", "false"},
      {"true", "true"},
      {"debug", "kDefaultForDebugOnly"}};
  std::map<std::string, std::string> allowed_platforms_ = {
      {"windows", "GPR_WINDOWS"}, {"ios", "GRPC_CFSTREAM"}, {"posix", ""}};
  std::map<std::string, std::string> final_return_ = {
      {"broken", "return false;"},
      {"false", "return false;"},
      {"true", "return true;"},
      {"debug",
       "\n#ifdef NDEBUG\nreturn false;\n#else\nreturn true;\n#endif\n"},
  };
  std::map<std::string, std::string> final_define_ = {
      {"broken", ""},
      {"false", ""},
      {"true", "#define %s"},
      {"debug", "#ifndef NDEBUG\n#define %s\n#endif\n"},
  };
  std::map<std::string, std::string> bzl_list_for_defaults_ = {
      {"broken", ""},
      {"false", "off"},
      {"true", "on"},
      {"debug", "dbg"},
  };
  const std::string experiments_defs_content_ = R"(
    - name: backoff_cap_initial_at_max
      description: Backoff library applies max_backoff even on initial_backoff.
      expiry: 2025/05/01
      owner: roth@google.com
      test_tags: []
      requires: [call_tracer_in_transport]
    - name: call_tracer_in_transport
      description: Transport directly passes byte counts to CallTracer.
      expiry: 2025/06/01
      owner: roth@google.com
      test_tags: []
      allow_in_fuzzing_config: false
      requires: [call_tracer_transport_fix]
    - name: call_tracer_transport_fix
      description: Use the correct call tracer in transport
      expiry: 2025/06/01
      owner: yashkt@google.com
      test_tags: []
    )";
  const std::string experiments_defs_content_with_circular_dependency_ = R"(
    - name: backoff_cap_initial_at_max
      description: Backoff library applies max_backoff even on initial_backoff.
      expiry: 2025/05/01
      owner: roth@google.com
      test_tags: []
      requires: [call_tracer_in_transport]
    - name: call_tracer_in_transport
      description: Transport directly passes byte counts to CallTracer.
      expiry: 2025/06/01
      owner: roth@google.com
      test_tags: []
      requires: [call_tracer_transport_fix]
    - name: call_tracer_transport_fix
      description: Use the correct call tracer in transport
      expiry: 2025/06/01
      owner: yashkt@google.com
      test_tags: []
      requires: [backoff_cap_initial_at_max]
    )";
  const std::string experiments_rollout_yaml_content_ = R"(
    - name: backoff_cap_initial_at_max
      default: true
    - name: call_tracer_in_transport
      default: false
    - name: call_tracer_transport_fix
      default: 
        ios: broken
        windows: false
        posix: debug
 )";
  grpc_core::ExperimentsCompiler compiler_;
};

TEST_F(ExperimentsCompilerTest, GenerateGrpcOssProductionExperimentsOutput) {
  EXPECT_OK(AddExperimentDefinition());
  EXPECT_OK(AddRolloutSpecification());
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
inline bool IsCallTracerTransportFixEnabled() { return false; }
inline bool IsCallTracerInTransportEnabled() { return false; }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return true; }

#elif defined(GPR_WINDOWS)
inline bool IsCallTracerTransportFixEnabled() { return false; }
inline bool IsCallTracerInTransportEnabled() { return false; }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return true; }

#else
#ifndef NDEBUG
#define GRPC_EXPERIMENT_IS_INCLUDED_CALL_TRACER_TRANSPORT_FIX

#endif
inline bool IsCallTracerTransportFixEnabled() { 
#ifdef NDEBUG
return false;
#else
return true;
#endif
 }
inline bool IsCallTracerInTransportEnabled() { return false; }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return true; }
#endif

#else
enum ExperimentIds {
  kExperimentIdCallTracerTransportFix,
  kExperimentIdCallTracerInTransport,
  kExperimentIdBackoffCapInitialAtMax,
  kNumExperiments
};
#define GRPC_EXPERIMENT_IS_INCLUDED_CALL_TRACER_TRANSPORT_FIX
inline bool IsCallTracerTransportFixEnabled() { return IsExperimentEnabled<kExperimentIdCallTracerTransportFix>(); }
#define GRPC_EXPERIMENT_IS_INCLUDED_CALL_TRACER_IN_TRANSPORT
inline bool IsCallTracerInTransportEnabled() { return IsExperimentEnabled<kExperimentIdCallTracerInTransport>(); }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return IsExperimentEnabled<kExperimentIdBackoffCapInitialAtMax>(); }

extern const ExperimentMetadata g_experiment_metadata[kNumExperiments];

#endif
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
)";
  std::string expected_src_output =
      grpc_core::GetCopyright() +
      "// Auto generated by "
      "tools/codegen/core/gen_experiments_grpc_oss.cc\n\n" +
      R"(
#include <grpc/support/port_platform.h>

#include <stdint.h>

#include "/tmp/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_call_tracer_transport_fix = "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_call_tracer_in_transport = "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const uint8_t required_experiments_call_tracer_in_transport[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerTransportFix)};
const char* const description_backoff_cap_initial_at_max = "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const uint8_t required_experiments_backoff_cap_initial_at_max[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerInTransport)};
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
  {"call_tracer_transport_fix", description_call_tracer_transport_fix, additional_constraints_call_tracer_transport_fix, nullptr, 0, false, true},
  {"call_tracer_in_transport", description_call_tracer_in_transport, additional_constraints_call_tracer_in_transport, required_experiments_call_tracer_in_transport, 1, false, false},
  {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max, additional_constraints_backoff_cap_initial_at_max, required_experiments_backoff_cap_initial_at_max, 1, true, true},
};

}  // namespace grpc_core

#elif defined(GPR_WINDOWS)
namespace {
const char* const description_call_tracer_transport_fix = "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_call_tracer_in_transport = "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const uint8_t required_experiments_call_tracer_in_transport[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerTransportFix)};
const char* const description_backoff_cap_initial_at_max = "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const uint8_t required_experiments_backoff_cap_initial_at_max[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerInTransport)};
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
  {"call_tracer_transport_fix", description_call_tracer_transport_fix, additional_constraints_call_tracer_transport_fix, nullptr, 0, false, true},
  {"call_tracer_in_transport", description_call_tracer_in_transport, additional_constraints_call_tracer_in_transport, required_experiments_call_tracer_in_transport, 1, false, false},
  {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max, additional_constraints_backoff_cap_initial_at_max, required_experiments_backoff_cap_initial_at_max, 1, true, true},
};

}  // namespace grpc_core

#else
namespace {
const char* const description_call_tracer_transport_fix = "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_call_tracer_in_transport = "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const uint8_t required_experiments_call_tracer_in_transport[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerTransportFix)};
const char* const description_backoff_cap_initial_at_max = "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const uint8_t required_experiments_backoff_cap_initial_at_max[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerInTransport)};
#ifdef NDEBUG
const bool kDefaultForDebugOnly = false;
#else
const bool kDefaultForDebugOnly = true;
#endif
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
  {"call_tracer_transport_fix", description_call_tracer_transport_fix, additional_constraints_call_tracer_transport_fix, nullptr, 0, kDefaultForDebugOnly, true},
  {"call_tracer_in_transport", description_call_tracer_in_transport, additional_constraints_call_tracer_in_transport, required_experiments_call_tracer_in_transport, 1, false, false},
  {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max, additional_constraints_backoff_cap_initial_at_max, required_experiments_backoff_cap_initial_at_max, 1, true, true},
};

}  // namespace grpc_core
#endif
#endif
)";
  EXPECT_EQ(expected_hdr_output, hdr_output);
  EXPECT_EQ(expected_src_output, src_output);
}

// Test the generated output for test mode. The main difference between
// production mode and test mode is that in test mode, the generated source
// file will use `g_test_experiment_metadata` instead of `g_experiment_metadata`
// to store the experiment metadata.
TEST_F(ExperimentsCompilerTest, GenerateGrpcOssTestExperimentsOutput) {
  EXPECT_OK(AddExperimentDefinition());
  EXPECT_OK(AddRolloutSpecification());
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
inline bool IsCallTracerTransportFixEnabled() { return false; }
inline bool IsCallTracerInTransportEnabled() { return false; }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return true; }

#elif defined(GPR_WINDOWS)
inline bool IsCallTracerTransportFixEnabled() { return false; }
inline bool IsCallTracerInTransportEnabled() { return false; }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return true; }

#else
#ifndef NDEBUG
#define GRPC_EXPERIMENT_IS_INCLUDED_CALL_TRACER_TRANSPORT_FIX

#endif
inline bool IsCallTracerTransportFixEnabled() { 
#ifdef NDEBUG
return false;
#else
return true;
#endif
 }
inline bool IsCallTracerInTransportEnabled() { return false; }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return true; }
#endif

#else
enum ExperimentIds {
  kExperimentIdCallTracerTransportFix,
  kExperimentIdCallTracerInTransport,
  kExperimentIdBackoffCapInitialAtMax,
  kNumExperiments
};
#define GRPC_EXPERIMENT_IS_INCLUDED_CALL_TRACER_TRANSPORT_FIX
inline bool IsCallTracerTransportFixEnabled() { return IsExperimentEnabled<kExperimentIdCallTracerTransportFix>(); }
#define GRPC_EXPERIMENT_IS_INCLUDED_CALL_TRACER_IN_TRANSPORT
inline bool IsCallTracerInTransportEnabled() { return IsExperimentEnabled<kExperimentIdCallTracerInTransport>(); }
#define GRPC_EXPERIMENT_IS_INCLUDED_BACKOFF_CAP_INITIAL_AT_MAX
inline bool IsBackoffCapInitialAtMaxEnabled() { return IsExperimentEnabled<kExperimentIdBackoffCapInitialAtMax>(); }

extern const ExperimentMetadata g_experiment_metadata[kNumExperiments];

#endif
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
)";
  std::string expected_src_output =
      grpc_core::GetCopyright() +
      "// Auto generated by "
      "tools/codegen/core/gen_experiments_grpc_oss.cc\n\n" +
      R"(
#include <grpc/support/port_platform.h>

#include <stdint.h>

#include "/tmp/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_call_tracer_transport_fix = "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_call_tracer_in_transport = "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const uint8_t required_experiments_call_tracer_in_transport[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerTransportFix)};
const char* const description_backoff_cap_initial_at_max = "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const uint8_t required_experiments_backoff_cap_initial_at_max[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerInTransport)};
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
  {"call_tracer_transport_fix", description_call_tracer_transport_fix, additional_constraints_call_tracer_transport_fix, nullptr, 0, false, true},
  {"call_tracer_in_transport", description_call_tracer_in_transport, additional_constraints_call_tracer_in_transport, required_experiments_call_tracer_in_transport, 1, false, false},
  {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max, additional_constraints_backoff_cap_initial_at_max, required_experiments_backoff_cap_initial_at_max, 1, true, true},
};

}  // namespace grpc_core

#elif defined(GPR_WINDOWS)
namespace {
const char* const description_call_tracer_transport_fix = "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_call_tracer_in_transport = "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const uint8_t required_experiments_call_tracer_in_transport[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerTransportFix)};
const char* const description_backoff_cap_initial_at_max = "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const uint8_t required_experiments_backoff_cap_initial_at_max[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerInTransport)};
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
  {"call_tracer_transport_fix", description_call_tracer_transport_fix, additional_constraints_call_tracer_transport_fix, nullptr, 0, false, true},
  {"call_tracer_in_transport", description_call_tracer_in_transport, additional_constraints_call_tracer_in_transport, required_experiments_call_tracer_in_transport, 1, false, false},
  {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max, additional_constraints_backoff_cap_initial_at_max, required_experiments_backoff_cap_initial_at_max, 1, true, true},
};

}  // namespace grpc_core

#else
namespace {
const char* const description_call_tracer_transport_fix = "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_call_tracer_in_transport = "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const uint8_t required_experiments_call_tracer_in_transport[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerTransportFix)};
const char* const description_backoff_cap_initial_at_max = "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const uint8_t required_experiments_backoff_cap_initial_at_max[] = {static_cast<uint8_t>(grpc_core::kExperimentIdCallTracerInTransport)};
#ifdef NDEBUG
const bool kDefaultForDebugOnly = false;
#else
const bool kDefaultForDebugOnly = true;
#endif
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
  {"call_tracer_transport_fix", description_call_tracer_transport_fix, additional_constraints_call_tracer_transport_fix, nullptr, 0, kDefaultForDebugOnly, true},
  {"call_tracer_in_transport", description_call_tracer_in_transport, additional_constraints_call_tracer_in_transport, required_experiments_call_tracer_in_transport, 1, false, false},
  {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max, additional_constraints_backoff_cap_initial_at_max, required_experiments_backoff_cap_initial_at_max, 1, true, true},
};

}  // namespace grpc_core
#endif
#endif
)";
  EXPECT_EQ(expected_hdr_output, hdr_output);
  EXPECT_EQ(expected_src_output, src_output);
}

TEST_F(ExperimentsCompilerTest, CheckCircularDependency) {
  EXPECT_OK(AddExperimentDefinitionWithCircularDependency());
  EXPECT_OK(AddRolloutSpecification());
  std::string hdr_filename = "/tmp/experiments.github.h";
  std::string src_filename = "/tmp/experiments.github.cc";
  EXPECT_THAT(GenerateExperimentsHdr(hdr_filename, "test"),
              ::testing::status::StatusIs(
                  absl::StatusCode::kInvalidArgument,
                  "Circular dependency found in experiment dependencies."));
  EXPECT_THAT(GenerateExperimentsSrc(src_filename, hdr_filename, "test"),
              ::testing::status::StatusIs(
                  absl::StatusCode::kInvalidArgument,
                  "Circular dependency found in experiment dependencies."));
}
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
