#ifndef GRPC_TOOLS_CODEGEN_CORE_EXPERIMENTS_COMPILER_H_
#define GRPC_TOOLS_CODEGEN_CORE_EXPERIMENTS_COMPILER_H_

#include <map>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {

struct RolloutSpecification {
  std::string name;
  // Either default_value or platform_value must be set.
  std::string default_value;
  std::map<std::string, std::string> platform_value;
  std::vector<std::string> requirements;
};

class ExperimentDefinition {
 public:
  ExperimentDefinition(const std::string& name = "",
                       const std::string& description = "",
                       const std::string& owner = "",
                       const std::string& expiry = "",
                       bool uses_polling = false,
                       bool allow_in_fuzzing_config = false,
                       const std::vector<std::string>& test_tags = {},
                       const std::vector<std::string>& requirements = {});

  bool IsValid(bool check_expiry = false) const;
  bool AddRolloutSpecification(
      const std::map<std::string, std::string>& defaults,
      const std::map<std::string, std::string>& platforms_define,
      RolloutSpecification& rollout_attributes);

  const std::string& name() const { return name_; }
  const std::string& owner() const { return owner_; }
  const std::string& expiry() const { return expiry_; }
  bool uses_polling() const { return uses_polling_; }
  const std::string& description() const { return description_; }
  std::string default_value(const std::string& platform) const {
    auto it = defaults_.find(platform);
    return it != defaults_.end() ? it->second : "false";
  }
  const std::vector<std::string>& test_tags() const { return test_tags_; }
  bool allow_in_fuzzing_config() const { return allow_in_fuzzing_config_; }
  std::string additional_constraints(const std::string& platform) const {
    auto it = additional_constraints_.find(platform);
    return it != additional_constraints_.end() ? it->second : "false";
  }
  const std::vector<std::string>& requirements() const { return requires_; }

 private:
  bool error_;
  std::string name_;
  std::string description_;
  std::string owner_;
  std::string expiry_;
  bool uses_polling_;
  bool allow_in_fuzzing_config_;
  std::vector<std::string> test_tags_;
  std::vector<std::string> requires_;
  std::map<std::string, std::string> defaults_;
  std::map<std::string, std::string> additional_constraints_;
};

class ExperimentsCompiler {
 public:
  ExperimentsCompiler(
      const std::map<std::string, std::string>& defaults,
      const std::map<std::string, std::string>& platforms_define,
      const std::map<std::string, std::string>& final_return,
      const std::map<std::string, std::string>& final_define,
      const std::map<std::string, std::string>& bzl_list_for_defaults)
      : defaults_(defaults),
        platforms_define_(platforms_define),
        final_return_(final_return),
        final_define_(final_define),
        bzl_list_for_defaults_(bzl_list_for_defaults) {}

  absl::Status AddExperimentDefinition(std::string experiments_yaml_content);
  absl::Status AddRolloutSpecification(
      std::string experiments_rollout_yaml_content);
  absl::Status GenerateExperimentsHdr(const std::string& output_file,
                                      const std::string& mode);
  absl::Status GenerateExperimentsSrc(const std::string& output_file,
                                      const std::string& header_file_path,
                                      const std::string& mode);
  void GenerateTest(const std::string& output_file);
  void GenExperimentsBzl(const std::string& output_file,
                         const std::string& mode);
  void EnsureNoDebugExperiments();

  const std::map<std::string, std::string>& defaults() const {
    return defaults_;
  }
  const std::map<std::string, std::string>& platforms_define() const {
    return platforms_define_;
  }
  const std::map<std::string, std::string>& final_return() const {
    return final_return_;
  }
  const std::map<std::string, std::string>& final_define() const {
    return final_define_;
  }
  const std::map<std::string, std::string>& bzl_list_for_defaults() const {
    return bzl_list_for_defaults_;
  }
  const std::map<std::string, ExperimentDefinition>& experiment_definitions()
      const {
    return experiment_definitions_;
  }

 private:
  absl::Status _WriteToFile(const std::string& output_file,
                            const std::string& contents);
  absl::StatusOr<std::string> _GenerateExperimentsHdr(const std::string& mode);
  absl::StatusOr<std::string> _GenerateExperimentsSrc(
      const std::string& header_file_path, const std::string& mode);
  const std::map<std::string, std::string>& defaults_;
  const std::map<std::string, std::string>& platforms_define_;
  const std::map<std::string, std::string>& final_return_;
  const std::map<std::string, std::string>& final_define_;
  const std::map<std::string, std::string>& bzl_list_for_defaults_;
  std::map<std::string, ExperimentDefinition> experiment_definitions_;
};

static inline std::string GetCopyright() {
  absl::CivilDay today = absl::ToCivilDay(absl::Now(), absl::UTCTimeZone());
  return absl::StrCat(
      "// Copyright ", absl::CivilYear(today).year(), " The gRPC Authors\n",
      "// Licensed under the Apache "
      "License, // version 2.0 (the \"License\");\n// you may not use this "
      "file except in // compliance with the License.\n// You may obtain a "
      "copy of the License at\n// \n//     "
      "http://www.apache.org/licenses/LICENSE-2.0\n// \n// Unless required by "
      "applicable law or // agreed to in writing, software\n// distributed "
      "under the License is // distributed on an \"AS IS\" BASIS,\n// WITHOUT "
      "WARRANTIES OR CONDITIONS OF ANY // KIND, either express or implied.\n// "
      "See the License for the specific // language governing permissions "
      "and\n// limitations under the License.\n//\n//\n//\n");
}

const char _GRPC_CODEGEN_PLACEHOLDER_TEXT[] = R"(
  This file contains the autogenerated parts of the experiments API.
  
  It generates two symbols for each experiment.
  
  For the experiment named new_car_project, it generates:
  
  - a function IsNewCarProjectEnabled() that returns true if the experiment
    should be enabled at runtime.
  
  - a macro GRPC_EXPERIMENT_IS_INCLUDED_NEW_CAR_PROJECT that is defined if the
    experiment *could* be enabled at runtime.
  
  The function is used to determine whether to run the experiment or
  non-experiment code path.
  
  If the experiment brings significant bloat, the macro can be used to avoid
  including the experiment code path in the binary for binaries that are size
  sensitive.
  
  By default that includes our iOS and Android builds.
  
  Finally, a small array is included that contains the metadata for each
  experiment.
  
  A macro, GRPC_EXPERIMENTS_ARE_FINAL, controls whether we fix experiment
  configuration at build time (if it's defined) or allow it to be tuned at
  runtime (if it's disabled).
  
  If you are using the Bazel build system, that macro can be configured with
  --define=grpc_experiments_are_final=true
  )";

static inline std::string GetGrpcCodegenPlaceholderText() {
  return _GRPC_CODEGEN_PLACEHOLDER_TEXT;
}

class ExperimentsOutputGenerator {
 public:
  ExperimentsOutputGenerator(ExperimentsCompiler& compiler)
      : compiler_(compiler) {}
  virtual ~ExperimentsOutputGenerator() = default;
  virtual void GenerateHeader(std::string& output) = 0;
  virtual void GenerateSource(std::string& output) = 0;

 protected:
  void PutCopyright(std::string& output);
  void PutBanner(const std::string& prefix, std::vector<std::string>& lines,
                 std::string& output);
  std::string SnakeToPascal(const std::string& snake_case);
  void _GenerateHeader(std::string& output, const std::string& mode = "");
  void _GenerateSource(std::string& output, const std::string& header_file_path,
                       const std::string& mode = "");
  void _GenerateExperimentsHdrForPlatform(std::string& output,
                                          const std::string& platform);
  void _GenerateExperimentsSrcForPlatform(std::string& output,
                                          const std::string& platform,
                                          const std::string& mode = "");

 private:
  ExperimentsCompiler& compiler_;
};

class GrpcGoogle3ExperimentsOutputGenerator
    : public ExperimentsOutputGenerator {
 public:
  GrpcGoogle3ExperimentsOutputGenerator(
      ExperimentsCompiler& compiler, const std::string& header_file_path = "")
      : ExperimentsOutputGenerator(compiler),
        header_file_path_(header_file_path) {}
  ~GrpcGoogle3ExperimentsOutputGenerator() = default;
  void GenerateHeader(std::string& output) override;
  void GenerateSource(std::string& output) override;

 private:
  const std::string header_file_path_;
};

class GrpcOssExperimentsOutputGenerator : public ExperimentsOutputGenerator {
 public:
  explicit GrpcOssExperimentsOutputGenerator(ExperimentsCompiler& compiler,
                                    const std::string& mode,
                                    const std::string& header_file_path = "")
      : ExperimentsOutputGenerator(compiler),
        mode_(mode),
        header_file_path_(header_file_path) {}
  ~GrpcOssExperimentsOutputGenerator() = default;
  void GenerateHeader(std::string& output) override;
  void GenerateSource(std::string& output) override;

 private:
  const std::string mode_;
  const std::string header_file_path_;
};

}  // namespace grpc_core

#endif  // GRPC_TOOLS_CODEGEN_CORE_EXPERIMENTS_COMPILER_H_
