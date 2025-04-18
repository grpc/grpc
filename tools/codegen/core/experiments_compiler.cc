#include "tools/codegen/core/experiments_compiler.h"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "third_party/yamlcpp/wrapped/yaml_cpp_wrapped.h"

namespace grpc_core {

ExperimentDefinition::ExperimentDefinition(
    const std::string& name, const std::string& description,
    const std::string& owner, const std::string& expiry, bool uses_polling,
    bool allow_in_fuzzing_config, const std::vector<std::string>& test_tags,
    const std::vector<std::string>& requirements)
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
      requires_.push_back(requirement);
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
    if (!rollout_attributes.default_value.empty()) {
      default_value = rollout_attributes.default_value;
    } else {
      if (rollout_attributes.platform_value.find(platform.first) ==
          rollout_attributes.platform_value.end()) {
        LOG(ERROR) << "ERROR: no value set for experiment "
                   << rollout_attributes.name << " on platform "
                   << platform.first;
        error_ = true;
        return false;
      } else {
        default_value = rollout_attributes.platform_value[platform.first];
      }
    }
    if (requires_.empty()) {
      defaults_[platform.first] = default_value;
      additional_constraints_[platform.first] = {};
    } else {
      // debug is assumed for all rollouts with additional constraints
      defaults_[platform.first] = "debug";
      additional_constraints_[platform.first] = absl::StrJoin(requires_, ", ");
    }
  }
  return true;
}

absl::Status ExperimentsCompiler::AddExperimentDefinition(
    std::string experiments_yaml_content) {
  // Process the yaml content and add the experiment definitions to the map.
  auto result = yaml_cpp_wrapped::YamlLoadAll(experiments_yaml_content);
  if (!result.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse yaml: ", result.status().ToString()));
  }
  for (const auto& value : result.value()) {
    if (value.IsMap()) {
      ExperimentDefinition experiment_definition(
          value["name"].as<std::string>(),
          value["description"].as<std::string>(),
          value["owner"].as<std::string>(), value["expiry"].as<std::string>(),
          value["uses_polling"].as<bool>(),
          value["allow_in_fuzzing_config"].as<bool>(),
          value["test_tags"].as<std::vector<std::string>>());
      LOG(INFO) << "Experiment definition: " << experiment_definition.name()
                << " " << experiment_definition.description() << " "
                << experiment_definition.owner() << " "
                << experiment_definition.expiry() << " "
                << experiment_definition.uses_polling() << " "
                << experiment_definition.allow_in_fuzzing_config();
      experiment_definitions_.emplace(value["name"].as<std::string>(),
                                      experiment_definition);
    }
  }
  return absl::OkStatus();
}

absl::Status ExperimentsCompiler::AddRolloutSpecification(
    std::string experiments_rollout_yaml_content) {
  // Process the yaml content and add the rollout specifications to the map.
  auto result = yaml_cpp_wrapped::YamlLoadAll(experiments_rollout_yaml_content);
  if (!result.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse yaml: ", result.status().ToString()));
  }
  for (const auto& value : result.value()) {
    if (value.IsMap()) {
      RolloutSpecification rollout_specification;
      if (value["default_value"].IsDefined()) {
        rollout_specification = RolloutSpecification(
            value["name"].as<std::string>(),
            value["default_value"].as<std::string>(),
            std::map<std::string, std::string>(), std::vector<std::string>());
      } else {
        if (!value["platform_value"].IsDefined()) {
          return absl::InvalidArgumentError(
              absl::StrCat("No default value or platform value for rollout: ",
                           value["name"].as<std::string>()));
        }
        rollout_specification = RolloutSpecification(
            value["name"].as<std::string>(), std::string(),
            value["platform_value"].as<std::map<std::string, std::string>>(),
            value["requirements"].as<std::vector<std::string>>());
      }
      bool success = experiment_definitions_[value["name"].as<std::string>()]
                         .AddRolloutSpecification(defaults_, platforms_define_,
                                                  rollout_specification);
      if (!success) {
        return absl::InvalidArgumentError(
            absl::StrCat("Failed to add rollout specification for experiment: ",
                         value["name"].as<std::string>()));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ExperimentsCompiler::_WriteToFile(const std::string& output_file,
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

absl::StatusOr<std::string> ExperimentsCompiler::_GenerateExperimentsHdr(
    const std::string& mode) {
  std::string output;
  if (mode == "grpc_google3") {
    GrpcGoogle3ExperimentsOutputGenerator generator(*this);
    generator.GenerateHeader(output);
  } else if (mode == "grpc_oss_production") {
    GrpcOssExperimentsOutputGenerator generator(*this, "production");
    generator.GenerateHeader(output);
  } else if (mode == "grpc_oss_test") {
    GrpcOssExperimentsOutputGenerator generator(*this, "test");
    generator.GenerateHeader(output);
  } else {
    LOG(ERROR) << "Unsupported mode: " << mode;
    return absl::InvalidArgumentError(absl::StrCat("Unsupported mode: ", mode));
  }
  return output;
}

absl::Status ExperimentsCompiler::GenerateExperimentsHdr(
    const std::string& output_file, const std::string& mode) {
  auto contents = _GenerateExperimentsHdr(mode);
  if (!contents.ok()) {
    return contents.status();
  }
  absl::Status status = _WriteToFile(output_file, contents.value());
  if (!status.ok()) {
    LOG(ERROR) << "Failed to write to file: " << output_file
               << " with error: " << status.message();
    return status;
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ExperimentsCompiler::_GenerateExperimentsSrc(
    const std::string& header_file_path, const std::string& mode) {
  std::string output;
  if (mode == "grpc_google3") {
    GrpcGoogle3ExperimentsOutputGenerator generator(*this, header_file_path);
    generator.GenerateSource(output);
  } else if (mode == "grpc_oss_production") {
    GrpcOssExperimentsOutputGenerator generator(*this, "production",
                                                header_file_path);
    generator.GenerateSource(output);
  } else if (mode == "grpc_oss_test") {
    GrpcOssExperimentsOutputGenerator generator(*this, "test",
                                                header_file_path);
    generator.GenerateSource(output);
  } else {
    LOG(ERROR) << "Unsupported mode: " << mode;
    return absl::InvalidArgumentError(absl::StrCat("Unsupported mode: ", mode));
  }
  return output;
}

absl::Status ExperimentsCompiler::GenerateExperimentsSrc(
    const std::string& output_file, const std::string& header_file_path,
    const std::string& mode) {
  auto contents = _GenerateExperimentsSrc(header_file_path, mode);
  if (!contents.ok()) {
    return contents.status();
  }
  absl::Status status = _WriteToFile(output_file, contents.value());
  if (!status.ok()) {
    LOG(ERROR) << "Failed to write to file: " << output_file
               << " with error: " << status.message();
    return status;
  }
  return absl::OkStatus();
}

void ExperimentsOutputGenerator::PutCopyright(std::string& output) {
  absl::StrAppend(&output, GetCopyright());
}

void ExperimentsOutputGenerator::PutBanner(const std::string& prefix,
                                           std::vector<std::string>& lines,
                                           std::string& output) {
  for (const auto& line : lines) {
    absl::StrAppend(&output, prefix, line, "\n");
  }
}

std::string ExperimentsOutputGenerator::SnakeToPascal(
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

void ExperimentsOutputGenerator::_GenerateExperimentsHdrForPlatform(
    std::string& output, const std::string& platform) {
  for (const auto& experiment : compiler_.experiment_definitions()) {
    std::string define_fmt =
        compiler_.final_define().at(experiment.second.default_value(platform));
    if (!define_fmt.empty()) {
      absl::StrAppend(&output, define_fmt, "GRPC_EXPERIMENT_IS_INCLUDED_",
                      absl::AsciiStrToUpper(experiment.second.name()), "\n");
    }
    absl::StrAppend(
        &output, "inline bool Is", SnakeToPascal(experiment.second.name()),
        "Enabled() { return ",
        compiler_.final_return().at(experiment.second.default_value(platform)),
        "; }\n");
  }
}

void ExperimentsOutputGenerator::_GenerateHeader(std::string& output,
                                                 const std::string& mode) {
  // Generate the #include for the header file.
  std::string include_guard = "GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H";
  absl::StrAppend(&output, "#ifndef ", include_guard, "\n");
  absl::StrAppend(&output, "#define ", include_guard, "\n");
  absl::StrAppend(&output, "#include <grpc/support/port_platform.h>\n");
  absl::StrAppend(&output, "#include \"src/core/lib/experiments/config.h\"\n");
  absl::StrAppend(&output, "namespace grpc_core {\n");
  absl::StrAppend(&output, "#ifdef GRPC_EXPERIMENTS_ARE_FINAL\n");
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
      absl::StrAppend(&output, "elif defined(", platform.second, ")\n");
    }
    _GenerateExperimentsHdrForPlatform(output, platform.first);
  }
  // Generate  #if defined for posix platform.
  absl::StrAppend(&output, "\n#else\n");
  _GenerateExperimentsHdrForPlatform(output, "posix");
  absl::StrAppend(&output, "#endif\n");
  absl::StrAppend(&output, "\n#else\n");
  std::string num_experiments_var_name = "kNumExperiments";
  std::string experiments_metadata_var_name = "g_experiment_metadata";
  absl::StrAppend(&output, " enum ExperimentIds {\n");
  for (const auto& experiment : compiler_.experiment_definitions()) {
    absl::StrAppend(&output, "  kExperimentId{",
                    SnakeToPascal(experiment.second.name()), ",\n");
  }
  absl::StrAppend(&output, "  {", num_experiments_var_name, "}\n};\n");
  for (const auto& experiment : compiler_.experiment_definitions()) {
    absl::StrAppend(&output, "#define GRPC_EXPERIMENT_IS_INCLUDED_",
                    absl::AsciiStrToUpper(experiment.second.name()), "\n");
    absl::StrAppend(&output, "inline bool Is",
                    SnakeToPascal(experiment.second.name()),
                    "Enabled() { return IsExperimentEnabled<kExperimentId",
                    SnakeToPascal(experiment.second.name()), ">(); }\n");
  }
  absl::StrAppend(&output, "extern const ExperimentMetadata",
                  experiments_metadata_var_name, "[", num_experiments_var_name,
                  "];\n");
  absl::StrAppend(&output, "#endif\n");
  absl::StrAppend(&output, "}  // namespace grpc_core\n");
  absl::StrAppend(&output, "#endif  // ", include_guard, "\n");
}

void ExperimentsOutputGenerator::_GenerateExperimentsSrcForPlatform(
    std::string& output, const std::string& platform, const std::string& mode) {
  absl::StrAppend(&output, "namespace {\n");
  bool default_for_debug_only = false;
  for (const auto& experiment : compiler_.experiment_definitions()) {
    absl::StrAppend(&output,
                    absl::StrFormat("const char* const description_%s = %s;\n",
                                    experiment.second.name(),
                                    experiment.second.description()));
    absl::StrAppend(
        &output,
        absl::StrFormat("const char* const additional_constraints_%s = %s;\n",
                        experiment.second.name(),
                        experiment.second.additional_constraints(platform)));
    if (!experiment.second.requirements().empty()) {
      std::vector<std::string> required_experiments;
      for (const auto& requirement : experiment.second.requirements()) {
        required_experiments.push_back(
            absl::StrFormat("static_cast<uint8_t>(grpc_core::kExperimentId%s)",
                            SnakeToPascal(requirement)));
      }
      absl::StrAppend(
          &output,
          absl::StrFormat("const uint8_t required_experiments_%s[] = {%s};\n",
                          experiment.second.name(),
                          absl::StrJoin(required_experiments, ",")));
    }
    if (compiler_.defaults().at(experiment.second.default_value(platform)) ==
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
  absl::StrAppend(&output, "}\n\n");
  absl::StrAppend(&output, "namespace grpc_core {\n\n");
  std::string experiments_metadata_var_name;
  if (mode == "test") {
    experiments_metadata_var_name = "g_test_experiment_metadata";
  } else {
    experiments_metadata_var_name = "g_experiment_metadata";
  }
  absl::StrAppend(&output, "const ExperimentMetadata ",
                  experiments_metadata_var_name, "[] = {\n");
  for (const auto& experiment : compiler_.experiment_definitions()) {
    absl::StrAppend(
        &output,
        absl::StrFormat(
            "  {%s, description_%s, additional_constraints_%s, %s, %d, %s, "
            "%s},",
            experiment.second.name().c_str(), experiment.second.name(),
            experiment.second.name(),
            absl::StrFormat(
                "required_experiments_%s",
                experiment.second.requirements().empty()
                    ? "nullptr"
                    : absl::StrJoin(experiment.second.requirements(), ", ")),
            experiment.second.requirements().size(),
            compiler_.defaults().at(experiment.second.default_value(platform)),
            experiment.second.allow_in_fuzzing_config() ? "true" : "false"));
  }
  absl::StrAppend(&output, "};\n\n");
  absl::StrAppend(&output, "}  // namespace grpc_core\n");
}

void ExperimentsOutputGenerator::_GenerateSource(
    std::string& output, const std::string& header_file_path,
    const std::string& mode) {
  bool any_requires;
  for (const auto& experiment : compiler_.experiment_definitions()) {
    if (!experiment.second.requirements().empty()) {
      any_requires = true;
      break;
    }
  }
  absl::StrAppend(&output, "#include <grpc/support/port_platform.h>\n\n");
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
                  "\"\n");
  absl::StrAppend(&output, "#ifdef GRPC_EXPERIMENTS_ARE_FINAL\n");
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
      absl::StrAppend(&output, "elif defined(", platform.second, ")\n");
    }
    _GenerateExperimentsSrcForPlatform(output, platform.first, mode);
  }
  // Generate  #if defined for posix platform.
  absl::StrAppend(&output, "\n#else\n");
  _GenerateExperimentsSrcForPlatform(output, "posix", mode);
  absl::StrAppend(&output, "#endif\n");
  absl::StrAppend(&output, "\n#endif\n");
}

void GrpcGoogle3ExperimentsOutputGenerator::GenerateHeader(
    std::string& output) {
  std::vector<std::string> lines;
  lines.push_back(
      " Auto generated by tools/codegen/core/gen_experiments_grpc_google3.cc");
  lines.push_back(GetGrpcCodegenPlaceholderText());
  PutCopyright(output);
  PutBanner("//", lines, output);
  // Generate the experiment interfaces.
  _GenerateHeader(output);
}

void GrpcGoogle3ExperimentsOutputGenerator::GenerateSource(
    std::string& output) {
  std::vector<std::string> lines;
  lines.push_back(
      " Auto generated by tools/codegen/core/gen_experiments_grpc_google3.cc");
  PutCopyright(output);
  PutBanner("//", lines, output);
  // Generate the experiment interfaces.
  _GenerateSource(output, header_file_path_);
}

void GrpcOssExperimentsOutputGenerator::GenerateHeader(std::string& output) {
  std::vector<std::string> lines;
  lines.push_back(
      " Auto generated by tools/codegen/core/gen_experiments_grpc_oss.cc");
  lines.push_back(GetGrpcCodegenPlaceholderText());
  PutCopyright(output);
  PutBanner("//", lines, output);
  // Generate the experiment interfaces.
  _GenerateHeader(output, mode_);
}

void GrpcOssExperimentsOutputGenerator::GenerateSource(std::string& output) {
  std::vector<std::string> lines;
  lines.push_back(
      " Auto generated by tools/codegen/core/gen_experiments_grpc_oss.cc");
  PutCopyright(output);
  PutBanner("//", lines, output);
  _GenerateSource(output, header_file_path_, mode_);
}
}  // namespace grpc_core
