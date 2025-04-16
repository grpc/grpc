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

/*
 * Generate experiment related code artifacts.
 *
 * Invoke as: tools/codegen/core/gen_experiments.cc
 * Experiment definitions are in src/core/lib/experiments/experiments.yaml
 */

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "tools/codegen/core/gen_experiments/experiments_compiler.h"

ABSL_FLAG(bool, check_expiry_dates, false, "Checks experiment expiry dates.");

ABSL_FLAG(bool, no_dbg_experiments, false,
          "If set to true, prohibit 'debug' configurations.");

ABSL_FLAG(std::string, repo_root, "", "Root directory of the repo.");

const std::map<std::string, std::string> DEFAULTS = {
    {"broken", "false"},
    {"false", "false"},
    {"true", "true"},
    {"debug", "kDefaultForDebugOnly"},
};

const std::map<std::string, std::string> PLATFORMS_DEFINE = {
    {"windows", "GPR_WINDOWS"},
    {"ios", "GRPC_CFSTREAM"},
    {"posix", ""},
};

const std::map<std::string, std::string> FINAL_RETURN = {
    {"broken", "return false;"},
    {"false", "return false;"},
    {"true", "return true;"},
    {"debug", R"(
#ifdef NDEBUG
return false;
#else
return true;
#endif
)"},
};

const std::map<std::string, std::string> FINAL_DEFINE = {
    {"broken", ""},
    {"false", ""},
    {"true", "#define %s"},
    {"debug", R"(#ifndef NDEBUG
#define %s#endif)"},
};

const std::map<std::string, std::string> BZL_LIST_FOR_DEFAULTS = {
    {"broken", ""},
    {"false", "off"},
    {"true", "on"},
    {"debug", "dbg"},
};

std::string InjectGithubPath(const std::string& path_str) {
  std::vector<std::string> base_path = absl::StrSplit(path_str, '.');
  return base_path[0] + ".github/" + base_path[1];
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

absl::Status GenerateExperimentFiles(const std::string& mode,
                                     const std::string& repo_root) {
  std::string experiments_defs_file;
  std::string experiments_rollouts_file;
  std::string experiments_hdr_file;
  std::string experiments_src_file;
  std::string experiments_bzl_file;

  if (mode == "test") {
    experiments_defs_file =
        "test/core/experiments/fixtures/test_experiments.yaml";
    experiments_rollouts_file =
        "test/core/experiments/fixtures/test_experiments_rollout.yaml";
    experiments_hdr_file = "test/core/experiments/fixtures/experiments.h";
    experiments_src_file = "test/core/experiments/fixtures/experiments.cc";
    experiments_bzl_file = "bazel/test_experiments.bzl";
  } else {
    experiments_defs_file = "src/core/lib/experiments/experiments.yaml";
    experiments_rollouts_file = "src/core/lib/experiments/rollouts.yaml";
    experiments_hdr_file = "src/core/lib/experiments/experiments.h";
    experiments_src_file = "src/core/lib/experiments/experiments.cc";
    experiments_bzl_file = "bazel/experiments.bzl";
    if (absl::StrContains(repo_root, "/google3/")) {
      experiments_rollouts_file = InjectGithubPath(experiments_rollouts_file);
      experiments_hdr_file = InjectGithubPath(experiments_hdr_file);
      experiments_src_file = InjectGithubPath(experiments_src_file);
      experiments_bzl_file = InjectGithubPath(experiments_bzl_file);
    }
  }
  // Read experiments.yaml file.
  std::string experiments_defs_content;
  absl::Status status =
      ReadFile(experiments_defs_file, experiments_defs_content);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to read experiments.yaml file: " << status.message()
               << "\n";
    return status;
  }
  // Read rollouts.yaml file.
  std::string experiments_rollouts_content;
  status = ReadFile(experiments_rollouts_file, experiments_rollouts_content);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to read rollouts.yaml file: " << status.message()
               << "\n";
    return status;
  }
  // Call experiments compiler to generate experiment files.
  grpc_core::ExperimentsCompiler experiments_compiler(
      DEFAULTS, PLATFORMS_DEFINE, FINAL_RETURN, FINAL_DEFINE,
      BZL_LIST_FOR_DEFAULTS);
  status =
      experiments_compiler.AddExperimentDefinition(experiments_defs_content);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add experiment definition: " << status.message()
               << "\n";
    return status;
  }
  status = experiments_compiler.AddRolloutSpecification(
      experiments_rollouts_content);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add rollout specification: " << status.message()
               << "\n";
    return status;
  }
  grpc_core::GrpcOssExperimentsOutputGenerator generator(
      experiments_compiler, mode, experiments_hdr_file);
  status = experiments_compiler.GenerateExperimentsHdr(experiments_hdr_file,
                                                       generator);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to generate experiment files: " << status.message()
               << "\n";
    return status;
  }
  status = experiments_compiler.GenerateExperimentsSrc(
      experiments_src_file, experiments_hdr_file, generator);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to generate experiment files: " << status.message()
               << "\n";
    return status;
  }
  return absl::OkStatus();
}

int main(int argc, char** argv) {
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  // Generate experiment files.
  absl::Status status =
      GenerateExperimentFiles("production", absl::GetFlag(FLAGS_repo_root));
  if (!status.ok()) {
    LOG(ERROR) << "Failed to generate experiment files: " << status.message()
               << "\n";
    return 1;
  }
  // Generate experiment files for test.
  status = GenerateExperimentFiles("test", absl::GetFlag(FLAGS_repo_root));
  if (!status.ok()) {
    LOG(ERROR) << "Failed to generate experiment files for test: "
               << status.message() << "\n";
    return 1;
  }
  return 0;
}
