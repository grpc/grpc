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

#include <cctype>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <ios>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "third_party/yamlcpp/include/yaml-cpp/node/emit.h"
#include "third_party/yamlcpp/include/yaml-cpp/node/node.h"
#include "third_party/yamlcpp/include/yaml-cpp/node/parse.h"
#include "third_party/yamlcpp/include/yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace grpc_core {

ExperimentDefinition::ExperimentDefinition(
    const std::string& name, const std::string& description,
    const std::string& owner, const std::string& expiry, bool uses_polling,
    bool allow_in_fuzzing_config, const std::vector<std::string>& test_tags,
    const std::set<std::string>& requirements)
    : error_(false),
      name_(name),
      description_(description),
      owner_(owner),
      expiry_(expiry),
      uses_polling_(uses_polling),
      allow_in_fuzzing_config_(allow_in_fuzzing_config),
      test_tags_(test_tags),
      requires_(requirements) {
  if (name.empty()) {
    LOG(ERROR) << "ERROR: experiment with no name";
    error_ = true;
  }
  if (description.empty()) {
    LOG(ERROR) << "ERROR: no description for experiment " << name_;
    error_ = true;
  }
  if (owner.empty()) {
    LOG(ERROR) << "ERROR: no owner for experiment " << name_;
    error_ = true;
  }
  if (expiry.empty()) {
    LOG(ERROR) << "ERROR: no expiry for experiment " << name_;
    error_ = true;
  }
  if (name_ == "monitoring_experiment" && expiry_ != "never-ever") {
    LOG(ERROR) << "ERROR: monitoring_experiment should never expire";
    error_ = true;
  }
  if (error_) {
    LOG(ERROR) << "Failed to create experiment definition";
  }
}

bool ExperimentDefinition::IsValid(bool check_expiry) const {
  if (error_) {
    return false;
  }
  if (name_ == "monitoring_experiment" && expiry_ == "never-ever") {
    return true;
  }
  absl::Time expiry_time;
  std::string error;
  if (!absl::ParseTime("%Y-%m-%d", expiry_, &expiry_time, &error)) {
    LOG(ERROR) << "ERROR: Invalid date format in expiry: " << expiry_
               << " for experiment " << name_;
    return false;
  }
  absl::CivilMonth expiry_month =
      absl::ToCivilMonth(expiry_time, absl::UTCTimeZone());
  absl::CivilDay expiry_day =
      absl::ToCivilDay(expiry_time, absl::UTCTimeZone());
  if (expiry_month.month() == 11 || expiry_month.month() == 12 ||
      (expiry_month.month() == 1 && expiry_day.day() < 15)) {
    LOG(ERROR) << "For experiment " << name_
               << ": Experiment expiration is not allowed between Nov 1 and "
                  "Jan 15 (experiment lists "
               << expiry_ << ").";
    return false;
  }
  if (!check_expiry) {
    return true;
  }
  if (expiry_time < absl::Now()) {
    LOG(WARNING) << "WARNING: experiment " << name_ << " expired on "
                 << expiry_;
  }
  absl::Time two_quarters_from_now = absl::Now() + absl::Hours(180 * 24);
  if (expiry_time > two_quarters_from_now) {
    LOG(WARNING) << "WARNING: experiment " << name_
                 << " expires far in the future on " << expiry_;
    LOG(WARNING) << "expiry should be no more than two quarters from now";
  }
  return !error_;
}

bool ExperimentDefinition::AddRolloutSpecification(
    const std::map<std::string, std::string>& defaults,
    const std::map<std::string, std::string>& platforms_define,
    RolloutSpecification& rollout_attributes) {
  if (error_) {
    return false;
  }
  if (rollout_attributes.name != name_) {
    LOG(ERROR)
        << "ERROR: Rollout specification does not apply to this experiment: "
        << name_;
    return false;
  }
  if (!rollout_attributes.requirements.empty()) {
    for (const auto& requirement : rollout_attributes.requirements) {
      requires_.insert(requirement);
    }
  }
  if (rollout_attributes.default_value.empty() &&
      rollout_attributes.platform_value.empty()) {
    LOG(ERROR) << "ERROR: no default for experiment "
               << rollout_attributes.name;
    error_ = true;
    return false;
  }
  for (const auto& platform : platforms_define) {
    std::string default_value;
    std::string additional_constraints;
    if (!rollout_attributes.default_value.empty()) {
      default_value = rollout_attributes.default_value;
    } else {
      if (rollout_attributes.platform_value.find(platform.first) ==
          rollout_attributes.platform_value.end()) {
        LOG(WARNING) << "WARNING: no value set for experiment "
                     << rollout_attributes.name << " on platform "
                     << platform.first;
        default_value = "false";
      } else {
        std::string platform_value =
            rollout_attributes.platform_value.at(platform.first);
        if (absl::StrContains(platform_value, "allowed_cells")) {
          // debug is assumed for all rollouts with additional constraints.
          default_value = "debug";
          additional_constraints = platform_value;
        } else {
          default_value = platform_value;
        }
      }
    }
    defaults_[platform.first] = default_value;
    additional_constraints_[platform.first] = additional_constraints;
  }
  return true;
}

absl::StatusOr<ExperimentDefinition> CreateExperimentDefinition(
    const YAML::Node& value) {
  if (!value["name"].IsDefined()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Experiment definition is missing name."));
  }
  if (!value["description"].IsDefined()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Experiment definition is missing description: ", YAML::Dump(value)));
  }
  if (!value["owner"].IsDefined()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Experiment definition is missing owner: ", YAML::Dump(value)));
  }
  if (!value["expiry"].IsDefined()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Experiment definition is missing expiry: ", YAML::Dump(value)));
  }
  bool uses_polling = value["uses_polling"].IsDefined()
                          ? value["uses_polling"].as<bool>()
                          : false;
  bool allow_in_fuzzing_config =
      value["allow_in_fuzzing_config"].IsDefined()
          ? value["allow_in_fuzzing_config"].as<bool>()
          : true;
  std::vector<std::string> test_tags;
  if (value["test_tags"].IsDefined()) {
    test_tags = value["test_tags"].as<std::vector<std::string>>();
  }
  std::set<std::string> requirements;
  if (value["requires"].IsDefined()) {
    for (const auto& requirement :
         value["requires"].as<std::vector<std::string>>()) {
      requirements.insert(requirement);
    }
  }
  ExperimentDefinition experiment_definition(
      value["name"].as<std::string>(), value["description"].as<std::string>(),
      value["owner"].as<std::string>(), value["expiry"].as<std::string>(),
      uses_polling, allow_in_fuzzing_config, test_tags, requirements);
  return experiment_definition;
}

absl::Status ExperimentsCompiler::AddExperimentDefinition(
    std::string experiments_yaml_content) {
  // Process the yaml content and add the experiment definitions to the map.
  YAML::Node results = YAML::Load(experiments_yaml_content);
  for (const auto& value : results) {
    if (value.IsMap()) {
      auto experiment_definition = CreateExperimentDefinition(value);
      if (!experiment_definition.ok()) {
        return experiment_definition.status();
      }
      experiment_definitions_.emplace(experiment_definition->name(),
                                      experiment_definition.value());
    }
  }
  return absl::OkStatus();
}

absl::Status ExperimentsCompiler::AddRolloutSpecification(
    std::string experiments_rollout_yaml_content) {
  // Process the yaml content and add the rollout specifications to the map.
  YAML::Node results = YAML::Load(experiments_rollout_yaml_content);
  if (!results.IsSequence()) {
    return absl::InvalidArgumentError(
        "Rollout specification is not a sequence.");
  }
  for (const auto& value : results) {
    RolloutSpecification rollout_specification;
    if (!value["default"].IsMap()) {
      // default is a single value, either true or false.
      rollout_specification = RolloutSpecification(
          value["name"].as<std::string>(), value["default"].as<std::string>(),
          std::map<std::string, std::string>());
    } else {
      // default is a map of platform to value.
      rollout_specification = RolloutSpecification(
          value["name"].as<std::string>(), std::string(),
          value["default"].as<std::map<std::string, std::string>>());
    }
    auto it = experiment_definitions_.find(value["name"].as<std::string>());
    if (it == experiment_definitions_.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Experiment definition not found for rollout: ",
                       value["name"].as<std::string>()));
    }
    bool success = it->second.AddRolloutSpecification(
        defaults_, platforms_define_, rollout_specification);
    if (!success) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to add rollout specification for experiment: ",
                       value["name"].as<std::string>()));
    }
  }
  return absl::OkStatus();
}

absl::Status ExperimentsCompiler::WriteToFile(const std::string& output_file,
                                              const std::string& contents) {
  std::ofstream outfile(output_file);  // Open the file for writing
  if (outfile.is_open()) {
    // Write data to the file
    outfile << contents;
    outfile.close();
    // Check if the file was closed successfully
    if (!outfile.good()) {
      LOG(ERROR) << "Error: Failed to close file: " << output_file;
      return absl::InternalError("Failed to close file: " + output_file);
    }
  } else {
    LOG(ERROR) << "Error: Failed to open file: " << output_file;
    return absl::InternalError("Failed to open file: " + output_file);
  }
  return absl::OkStatus();
}

bool AreAllRequirementsSatisfied(const std::set<std::string>& done_experiments,
                                 const std::set<std::string>& requirements) {
  for (const auto& req : requirements) {
    if (done_experiments.find(req) == done_experiments.end()) {
      return false;
    }
  }
  return true;
}

std::optional<std::string> FindExperimentWithAllRequirementsSatisfied(
    const std::map<std::string, std::set<std::string>>& queue,
    const std::set<std::string>& done_experiments) {
  for (const auto& [name, requirements] : queue) {
    // Check if all required experiments are in the 'done_experiments' set.
    if (AreAllRequirementsSatisfied(done_experiments, requirements)) {
      return name;
    }
  }
  return std::nullopt;
}

absl::Status ExperimentsCompiler::FinalizeExperiments() {
  std::map<std::string, std::set<std::string>> queue;
  for (const auto& [name, definition] : experiment_definitions_) {
    queue[name] = definition.requirements();
  }
  std::set<std::string> done_experiments;
  std::vector<std::string> sorted_experiment_names;
  while (!queue.empty()) {
    auto take_name =
        FindExperimentWithAllRequirementsSatisfied(queue, done_experiments);
    if (!take_name.has_value()) {
      // If no experiment was found whose requirements are met, there's a
      // circular dependency.
      return absl::InvalidArgumentError(
          "Circular dependency found in experiment dependencies.");
    }
    done_experiments.insert(*take_name);
    sorted_experiment_names.push_back(*take_name);
    queue.erase(*take_name);
  }
  sorted_experiment_names_.swap(sorted_experiment_names);
  return absl::OkStatus();
}

absl::Status ExperimentsCompiler::GenerateExperimentsHdr(
    const std::string& output_file, ExperimentsOutputGenerator& generator) {
  GRPC_EXPERIMENTS_RETURN_IF_ERROR(FinalizeExperiments());
  std::string output;
  generator.GenerateHeader(output);
  absl::Status status = WriteToFile(output_file, output);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to write to file: " << output_file
               << " with error: " << status.message();
    return status;
  }
  return absl::OkStatus();
}

absl::Status ExperimentsCompiler::GenerateExperimentsSrc(
    const std::string& output_file, const std::string& header_file_path,
    ExperimentsCompiler::ExperimentsOutputGenerator& generator) {
  GRPC_EXPERIMENTS_RETURN_IF_ERROR(FinalizeExperiments());
  std::string output;
  generator.GenerateSource(output);
  absl::Status status = WriteToFile(output_file, output);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to write to file: " << output_file
               << " with error: " << status.message();
    return status;
  }
  return absl::OkStatus();
}

void ExperimentsCompiler::ExperimentsOutputGenerator::PutCopyright(
    std::string& output) {
  absl::StrAppend(&output, GetCopyright());
}

void ExperimentsCompiler::ExperimentsOutputGenerator::PutBanner(
    const std::string& prefix, std::vector<std::string>& lines,
    std::string& output) {
  for (const auto& line : lines) {
    absl::StrAppend(&output, prefix, line, "\n");
  }
}

std::string ExperimentsCompiler::ExperimentsOutputGenerator::ToAsciiCStr(
    const std::string& s) {
  std::ostringstream result_stream;
  for (const char ch_char : s) {
    const unsigned char c = static_cast<const unsigned char>(ch_char);
    // Check if character is printable ASCII (32 to 126 inclusive).
    // AND not a backslash or double quote.
    if ((c >= 32 && c <= 126) && c != '\\' && c != '"') {
      result_stream << c;
    } else {
      // Escape with 3 digits, zero-padded, octal representation.
      result_stream << "\\" << std::oct << std::setw(3) << std::setfill('0')
                    << static_cast<int>(c);
      result_stream << std::dec;
    }
  }
  return result_stream.str();
}

std::string ExperimentsCompiler::ExperimentsOutputGenerator::SnakeToPascal(
    const std::string& snake_case) {
  std::stringstream pascal_case;
  bool capitalize_next = true;
  for (char c : snake_case) {
    if (c == '_') {
      capitalize_next = true;
    } else {
      if (capitalize_next) {
        pascal_case << static_cast<char>(std::toupper(c));
        capitalize_next = false;
      } else {
        pascal_case << c;
      }
    }
  }
  return pascal_case.str();
}

void ExperimentsCompiler::ExperimentsOutputGenerator::
    GenerateExperimentsHdrForPlatform(const std::string& platform,
                                      std::string& output) {
  for (const auto& experiment_name : compiler_.sorted_experiment_names()) {
    ExperimentDefinition experiment =
        compiler_.experiment_definitions().at(experiment_name);
    const std::string& define_fmt =
        compiler_.final_define().at(experiment.default_value(platform));
    // The define format is expected to either be empty or contain a single %s
    // specifier.
    if (!define_fmt.empty()) {
      std::string define_str = "GRPC_EXPERIMENT_IS_INCLUDED_" +
                               absl::AsciiStrToUpper(experiment.name()) + "\n";
      auto format = absl::ParsedFormat<'s'>::New(define_fmt);
      if (format) {
        absl::StrAppend(&output, absl::StrFormat(*format, define_str));
      } else {
        LOG(ERROR) << "Invalid format string: " << define_fmt;
      }
    }
    absl::StrAppend(
        &output, "inline bool Is", SnakeToPascal(experiment.name()),
        "Enabled() { ",
        compiler_.final_return().at(experiment.default_value(platform)),
        " }\n");
  }
}

void ExperimentsCompiler::ExperimentsOutputGenerator::GenerateHeaderInner(
    const std::string& mode, std::string& output) {
  // Generate the #include for the header file.
  std::string include_guard = "GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H";
  absl::StrAppend(&output, "\n#ifndef ", include_guard, "\n");
  absl::StrAppend(&output, "#define ", include_guard, "\n\n");
  absl::StrAppend(&output, "#include <grpc/support/port_platform.h>\n\n");
  absl::StrAppend(&output,
                  "#include \"src/core/lib/experiments/config.h\"\n\n");
  absl::StrAppend(&output, "namespace grpc_core {\n\n");
  absl::StrAppend(&output, "#ifdef GRPC_EXPERIMENTS_ARE_FINAL\n\n");
  // Generate the #if defined for each platform.
  bool first = true;
  for (const auto& platform : compiler_.platforms_define()) {
    if (platform.first == "posix") {
      continue;
    }
    if (first) {
      absl::StrAppend(&output, "#if defined(", platform.second, ")\n");
      first = false;
    } else {
      absl::StrAppend(&output, "\n#elif defined(", platform.second, ")\n");
    }
    GenerateExperimentsHdrForPlatform(platform.first, output);
  }
  // Generate  #if defined for posix platform.
  absl::StrAppend(&output, "\n#else\n");
  GenerateExperimentsHdrForPlatform("posix", output);
  absl::StrAppend(&output, "#endif\n");
  absl::StrAppend(&output, "\n#else\n");
  std::string num_experiments_var_name = "kNumExperiments";
  std::string experiments_metadata_var_name = "g_experiment_metadata";
  absl::StrAppend(&output, " enum ExperimentIds {\n");
  for (const auto& experiment_name : compiler_.sorted_experiment_names()) {
    absl::StrAppend(&output, "  kExperimentId", SnakeToPascal(experiment_name),
                    ",\n");
  }
  absl::StrAppend(&output, "  ", num_experiments_var_name, "\n};\n");
  for (const auto& experiment_name : compiler_.sorted_experiment_names()) {
    absl::StrAppend(&output, "#define GRPC_EXPERIMENT_IS_INCLUDED_",
                    absl::AsciiStrToUpper(experiment_name), "\n");
    absl::StrAppend(&output, "inline bool Is", SnakeToPascal(experiment_name),
                    "Enabled() { return IsExperimentEnabled<kExperimentId",
                    SnakeToPascal(experiment_name), ">(); }\n");
  }
  absl::StrAppend(&output, "\nextern const ExperimentMetadata ",
                  experiments_metadata_var_name, "[", num_experiments_var_name,
                  "];\n");
  absl::StrAppend(&output, "\n#endif\n");
  absl::StrAppend(&output, "}  // namespace grpc_core\n");
  absl::StrAppend(&output, "\n#endif  // ", include_guard, "\n");
}

void ExperimentsCompiler::ExperimentsOutputGenerator::
    GenerateExperimentsSrcForPlatform(const std::string& platform,
                                      const std::string& mode,
                                      std::string& output) {
  absl::StrAppend(&output, "namespace {\n");
  bool default_for_debug_only = false;
  for (const auto& experiment_name : compiler_.sorted_experiment_names()) {
    ExperimentDefinition experiment =
        compiler_.experiment_definitions().at(experiment_name);
    absl::StrAppend(
        &output, absl::StrFormat("const char* const description_%s = \"%s\";\n",
                                 experiment.name(),
                                 ToAsciiCStr(experiment.description())));
    absl::StrAppend(
        &output,
        absl::StrFormat(
            "const char* const additional_constraints_%s = \"{%s}\";\n",
            experiment.name(),
            ToAsciiCStr(experiment.additional_constraints(platform))));
    if (!experiment.requirements().empty()) {
      std::vector<std::string> required_experiments;
      for (const auto& requirement : experiment.requirements()) {
        required_experiments.push_back(
            absl::StrFormat("static_cast<uint8_t>(grpc_core::kExperimentId%s)",
                            SnakeToPascal(requirement)));
      }
      absl::StrAppend(
          &output,
          absl::StrFormat("const uint8_t required_experiments_%s[] = {%s};\n",
                          experiment.name(),
                          absl::StrJoin(required_experiments, ",")));
    }
    if (compiler_.defaults().at(experiment.default_value(platform)) ==
        "kDefaultForDebugOnly") {
      default_for_debug_only = true;
    }
  }
  if (default_for_debug_only) {
    absl::StrAppend(&output, "#ifdef NDEBUG\n");
    absl::StrAppend(&output, "const bool kDefaultForDebugOnly = false;\n");
    absl::StrAppend(&output, "#else\n");
    absl::StrAppend(&output, "const bool kDefaultForDebugOnly = true;\n");
    absl::StrAppend(&output, "#endif\n");
  }
  absl::StrAppend(&output, "}  // namespace\n\n");
  absl::StrAppend(&output, "namespace grpc_core {\n\n");
  std::string experiments_metadata_var_name;
  if (mode == "test") {
    experiments_metadata_var_name = "g_test_experiment_metadata";
  } else {
    experiments_metadata_var_name = "g_experiment_metadata";
  }
  absl::StrAppend(&output, "const ExperimentMetadata ",
                  experiments_metadata_var_name, "[] = {\n");
  for (const auto& experiment_name : compiler_.sorted_experiment_names()) {
    ExperimentDefinition experiment =
        compiler_.experiment_definitions().at(experiment_name);
    absl::StrAppend(
        &output,
        absl::StrFormat(
            "  {\"%s\", description_%s, additional_constraints_%s, %s, %d, %s, "
            "%s},\n",
            ToAsciiCStr(experiment.name()), experiment.name(),
            experiment.name(),
            experiment.requirements().empty()
                ? "nullptr"
                : absl::StrFormat("required_experiments_%s", experiment.name()),
            experiment.requirements().size(),
            compiler_.defaults().at(experiment.default_value(platform)),
            experiment.allow_in_fuzzing_config() ? "true" : "false"));
  }
  absl::StrAppend(&output, "};\n\n");
  absl::StrAppend(&output, "}  // namespace grpc_core\n");
}

void ExperimentsCompiler::ExperimentsOutputGenerator::GenerateSourceInner(
    const std::string& header_file_path, const std::string& mode,
    std::string& output) {
  bool any_requires = false;
  for (const auto& experiment : compiler_.experiment_definitions()) {
    if (!experiment.second.requirements().empty()) {
      any_requires = true;
      break;
    }
  }
  absl::StrAppend(&output, "\n#include <grpc/support/port_platform.h>\n\n");
  if (any_requires) {
    absl::StrAppend(&output, "#include <stdint.h>\n\n");
  }
  std::string github_suffix = ".github";
  size_t pos = header_file_path.find(github_suffix);
  std::string header_file_path_without_github = header_file_path;
  if (pos != std::string::npos) {
    header_file_path_without_github.replace(pos, github_suffix.size(), "");
  }
  absl::StrAppend(&output, "#include \"", header_file_path_without_github,
                  "\"\n\n");
  absl::StrAppend(&output, "#ifndef GRPC_EXPERIMENTS_ARE_FINAL\n");
  // Generate the #if defined for each platform.
  bool first = true;
  for (const auto& platform : compiler_.platforms_define()) {
    if (platform.first == "posix") {
      continue;
    }
    if (first) {
      absl::StrAppend(&output, "\n#if defined(", platform.second, ")\n");
      first = false;
    } else {
      absl::StrAppend(&output, "\n#elif defined(", platform.second, ")\n");
    }
    GenerateExperimentsSrcForPlatform(platform.first, mode, output);
  }
  // Generate  #if defined for posix platform.
  absl::StrAppend(&output, "\n#else\n");
  GenerateExperimentsSrcForPlatform("posix", mode, output);
  absl::StrAppend(&output, "#endif\n");
  absl::StrAppend(&output, "#endif\n");
}

void GrpcOssExperimentsOutputGenerator::GenerateHeader(std::string& output) {
  std::vector<std::string> lines;
  lines.push_back(
      " Auto generated by tools/codegen/core/gen_experiments_grpc_oss.cc");
  for (const auto& line :
       absl::StrSplit(GetGrpcCodegenPlaceholderText(), '\n')) {
    lines.push_back(std::string(line));
  }
  PutCopyright(output);
  PutBanner("//", lines, output);
  // Generate the experiment interfaces.
  GenerateHeaderInner(mode_, output);
}

void GrpcOssExperimentsOutputGenerator::GenerateSource(std::string& output) {
  std::vector<std::string> lines;
  lines.push_back(
      " Auto generated by tools/codegen/core/gen_experiments_grpc_oss.cc");
  PutCopyright(output);
  PutBanner("//", lines, output);
  // Generate the experiment interfaces.
  GenerateSourceInner(header_file_path_, mode_, output);
}
}  // namespace grpc_core
