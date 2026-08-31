/*
 *
 * Copyright 2015 gRPC authors.
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

// Generates Objective C gRPC service interface out of Protobuf IDL.

#include <google/protobuf/compiler/objectivec/names.h>

#include <fstream>
#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/objective_c_generator.h"
#include "src/compiler/objective_c_generator_helpers.h"

using ::google::protobuf::compiler::objectivec::
    IsProtobufLibraryBundledProtoFile;
using ::google::protobuf::compiler::objectivec::ProtobufLibraryFrameworkName;
#ifdef SUPPORT_OBJC_PREFIX_VALIDATION
using ::google::protobuf::compiler::objectivec::ValidateObjCClassPrefixes;
#endif
using ::grpc_objective_c_generator::FrameworkImport;
using ::grpc_objective_c_generator::LocalImport;
using ::grpc_objective_c_generator::PreprocIfElse;
using ::grpc_objective_c_generator::PreprocIfNot;
using ::grpc_objective_c_generator::SystemImport;

namespace {

bool ParseFrameworkMappings(const ::std::string& path,
                            ::std::map<::std::string, ::std::string>* mappings,
                            ::std::string* error) {
  ::std::ifstream file(path);
  if (!file.is_open()) {
    *error = "Unable to open framework mapping file: " + path;
    return false;
  }
  ::std::string line;
  while (::std::getline(file, line)) {
    size_t comment_pos = line.find('#');
    if (comment_pos != ::std::string::npos) {
      line.erase(comment_pos);
    }
    size_t first = line.find_first_not_of(" \t\r\n");
    if (first == ::std::string::npos) continue;
    size_t last = line.find_last_not_of(" \t\r\n");
    line = line.substr(first, last - first + 1);
    if (line.empty()) continue;

    size_t colon_pos = line.find(':');
    if (colon_pos == ::std::string::npos) {
      *error = "Framework/proto mapping line missing colon: '" + line + "'";
      return false;
    }
    ::std::string framework_name = line.substr(0, colon_pos);
    size_t fw_first = framework_name.find_first_not_of(" \t\r\n");
    size_t fw_last = framework_name.find_last_not_of(" \t\r\n");
    if (fw_first == ::std::string::npos) {
      *error = "Empty framework name in mapping line: '" + line + "'";
      return false;
    }
    framework_name = framework_name.substr(fw_first, fw_last - fw_first + 1);

    ::std::string proto_list = line.substr(colon_pos + 1);
    ::std::vector<::std::string> protos =
        grpc_generator::tokenize(proto_list, ",");
    for (const auto& proto : protos) {
      size_t p_first = proto.find_first_not_of(" \t\r\n");
      if (p_first == ::std::string::npos) continue;
      size_t p_last = proto.find_last_not_of(" \t\r\n");
      ::std::string proto_file = proto.substr(p_first, p_last - p_first + 1);
      if (!proto_file.empty()) {
        (*mappings)[proto_file] = framework_name;
      }
    }
  }
  return true;
}

inline ::std::string FrameworkForFile(
    const grpc::protobuf::FileDescriptor* file,
    const ::std::map<::std::string, ::std::string>& framework_mappings,
    const ::std::string& default_framework) {
  auto it = framework_mappings.find(std::string(file->name()));
  if (it != framework_mappings.end()) {
    return it->second;
  }
  return default_framework;
}

inline ::std::string ImportProtoHeaders(
    const grpc::protobuf::FileDescriptor* dep, const char* indent,
    const ::std::map<::std::string, ::std::string>& framework_mappings,
    const ::std::string& default_framework,
    const ::std::string& pb_runtime_import_prefix) {
  ::std::string header = grpc_objective_c_generator::MessageHeaderName(dep);

  if (!IsProtobufLibraryBundledProtoFile(dep)) {
    ::std::string framework =
        FrameworkForFile(dep, framework_mappings, default_framework);
    if (framework.empty()) {
      return indent + LocalImport(header);
    } else {
      return indent + FrameworkImport(header, framework);
    }
  }

  ::std::string base_name = header;
  grpc_generator::StripPrefix(&base_name, "google/protobuf/");
  ::std::string file_name = "GPB" + base_name;
  // create the import code snippet
  ::std::string framework_header =
      ::std::string(ProtobufLibraryFrameworkName) + "/" + file_name;
  ::std::string local_header = file_name;
  if (!pb_runtime_import_prefix.empty()) {
    local_header = pb_runtime_import_prefix + "/" + file_name;
  }

  static const ::std::string kFrameworkImportsCondition =
      "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS";
  return PreprocIfElse(kFrameworkImportsCondition,
                       indent + SystemImport(framework_header),
                       indent + LocalImport(local_header));
}

}  // namespace

class ObjectiveCGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  ObjectiveCGrpcGenerator() {}
  virtual ~ObjectiveCGrpcGenerator() {}

 public:
  uint64_t GetSupportedFeatures() const override {
    return FEATURE_PROTO3_OPTIONAL
#ifdef GRPC_PROTOBUF_EDITION_SUPPORT
           | FEATURE_SUPPORTS_EDITIONS
#endif
        ;
  }

#ifdef GRPC_PROTOBUF_EDITION_SUPPORT
  grpc::protobuf::Edition GetMinimumEdition() const override {
    return grpc::protobuf::Edition::EDITION_PROTO2;
  }
  grpc::protobuf::Edition GetMaximumEdition() const override {
    // TODO(yuanweiz): Remove when the protobuf is updated to a version
    //      that supports edition 2024.
#if !defined(GOOGLE_PROTOBUF_VERSION) || GOOGLE_PROTOBUF_VERSION >= 6032000
    return grpc::protobuf::Edition::EDITION_2024;
#else
    return grpc::protobuf::Edition::EDITION_2023;
#endif
  }
#endif

  virtual bool Generate(const grpc::protobuf::FileDescriptor* file,
                        const ::std::string& parameter,
                        grpc::protobuf::compiler::GeneratorContext* context,
                        ::std::string* error) const override {
    if (file->service_count() == 0) {
      // No services.  Do nothing.
      return true;
    }

#ifdef SUPPORT_OBJC_PREFIX_VALIDATION
    // Default options will use env variables for controls.
    if (!ValidateObjCClassPrefixes({file}, {}, error)) {
      return false;
    }
#endif

    bool grpc_local_import = false;
    ::std::string framework;
    ::std::string pb_runtime_import_prefix;
    ::std::string grpc_local_import_prefix;
    ::std::string framework_mappings_path;
    ::std::map<::std::string, ::std::string> framework_mappings;
    std::vector<::std::string> params_list =
        grpc_generator::tokenize(parameter, ",");
    for (auto param_str = params_list.begin(); param_str != params_list.end();
         ++param_str) {
      std::vector<::std::string> param =
          grpc_generator::tokenize(*param_str, "=");
      if (param[0] == "generate_for_named_framework") {
        if (param.size() != 2) {
          *error =
              std::string("Format: generate_for_named_framework=<Framework>");
          return false;
        } else if (param[1].empty()) {
          *error =
              std::string("Name of framework cannot be empty for parameter: ") +
              param[0];
          return false;
        }
        framework = param[1];
      } else if (param[0] == "named_framework_to_proto_path_mappings_path") {
        if (param.size() != 2) {
          *error = std::string(
              "Format: "
              "named_framework_to_proto_path_mappings_path=path/to/mappings");
          return false;
        } else if (param[1].empty()) {
          *error =
              std::string(
                  "Path to mappings file cannot be empty for parameter: ") +
              param[0];
          return false;
        }
        framework_mappings_path = param[1];
      } else if (param[0] == "runtime_import_prefix") {
        if (param.size() != 2) {
          *error = grpc::string("Format: runtime_import_prefix=dir/");
          return false;
        }
        pb_runtime_import_prefix = param[1];
        grpc_generator::StripSuffix(&pb_runtime_import_prefix, "/");
      } else if (param[0] == "grpc_local_import_prefix") {
        grpc_local_import = true;
        if (param.size() != 2) {
          *error = grpc::string("Format: grpc_local_import_prefix=dir/");
          return false;
        }
        grpc_local_import_prefix = param[1];
      }
    }

    if (!framework_mappings_path.empty()) {
      if (!ParseFrameworkMappings(framework_mappings_path, &framework_mappings,
                                  error)) {
        return false;
      }
    }

    static const ::std::string kNonNullBegin = "NS_ASSUME_NONNULL_BEGIN\n";
    static const ::std::string kNonNullEnd = "NS_ASSUME_NONNULL_END\n";
    static const ::std::string kProtocolOnly = "GPB_GRPC_PROTOCOL_ONLY";
    static const ::std::string kForwardDeclare =
        "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO";

    ::std::string file_name =
        google::protobuf::compiler::objectivec::FilePath(file);

    grpc_objective_c_generator::Parameters generator_params;
    generator_params.no_v1_compatibility = false;

    if (!parameter.empty()) {
      std::vector<std::string> parameters_list =
          grpc_generator::tokenize(parameter, ",");
      for (auto parameter_string = parameters_list.begin();
           parameter_string != parameters_list.end(); parameter_string++) {
        std::vector<std::string> param =
            grpc_generator::tokenize(*parameter_string, "=");
        if (param[0] == "no_v1_compatibility") {
          generator_params.no_v1_compatibility = true;
        }
      }
    }

    // Write out a file header.
    ::std::string file_header =
        "// Code generated by gRPC proto compiler.  DO NOT EDIT!\n"
        "// source: " +
        ::std::string(file->name()) + "\n\n";

    {
      // Generate .pbrpc.h

      ::std::string file_framework =
          FrameworkForFile(file, framework_mappings, framework);
      ::std::string imports;
      if (file_framework.empty()) {
        imports = LocalImport(file_name + ".pbobjc.h");
      } else {
        imports = FrameworkImport(file_name + ".pbobjc.h", file_framework);
      }

      ::std::string system_imports;
      if (grpc_local_import) {
        system_imports =
            LocalImport(grpc_local_import_prefix + "ProtoRPC/ProtoService.h");
        if (generator_params.no_v1_compatibility) {
          system_imports +=
              LocalImport(grpc_local_import_prefix + "ProtoRPC/ProtoRPC.h");
        } else {
          system_imports += LocalImport(grpc_local_import_prefix +
                                        "ProtoRPC/ProtoRPCLegacy.h");
          system_imports += LocalImport(grpc_local_import_prefix +
                                        "RxLibrary/GRXWriteable.h");
          system_imports +=
              LocalImport(grpc_local_import_prefix + "RxLibrary/GRXWriter.h");
        }
      } else {
        system_imports = SystemImport("ProtoRPC/ProtoService.h");
        if (generator_params.no_v1_compatibility) {
          system_imports += SystemImport("ProtoRPC/ProtoRPC.h");
        } else {
          system_imports += SystemImport("ProtoRPC/ProtoRPCLegacy.h");
          system_imports += SystemImport("RxLibrary/GRXWriteable.h");
          system_imports += SystemImport("RxLibrary/GRXWriter.h");
        }
      }

      ::std::string forward_declarations =
          "@class GRPCUnaryProtoCall;\n"
          "@class GRPCStreamingProtoCall;\n"
          "@class GRPCCallOptions;\n"
          "@class GRXWriter;\n"
          "@protocol GRPCProtoResponseHandler;\n";
      if (!generator_params.no_v1_compatibility) {
        forward_declarations += "@class GRPCProtoCall;\n";
      }
      forward_declarations += "\n";

      ::std::string class_declarations =
          grpc_objective_c_generator::GetAllMessageClasses(file);

      ::std::string class_imports;
      for (int i = 0; i < file->dependency_count(); i++) {
        class_imports +=
            ImportProtoHeaders(file->dependency(i), "  ", framework_mappings,
                               framework, pb_runtime_import_prefix);
      }

      ::std::string ng_protocols;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        ng_protocols += grpc_objective_c_generator::GetV2Protocol(service);
      }

      ::std::string protocols;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        protocols +=
            grpc_objective_c_generator::GetProtocol(service, generator_params);
      }

      ::std::string interfaces;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        interfaces +=
            grpc_objective_c_generator::GetInterface(service, generator_params);
      }

      Write(context, file_name + ".pbrpc.h",
            file_header + SystemImport("Foundation/Foundation.h") + "\n" +
                PreprocIfNot(kForwardDeclare, imports) + "\n" +
                PreprocIfNot(kProtocolOnly, system_imports) + "\n" +
                class_declarations + "\n" +
                PreprocIfNot(kForwardDeclare, class_imports) + "\n" +
                forward_declarations + "\n" + kNonNullBegin + "\n" +
                ng_protocols + protocols + "\n" +
                PreprocIfNot(kProtocolOnly, interfaces) + "\n" + kNonNullEnd +
                "\n");
    }

    {
      // Generate .pbrpc.m

      ::std::string file_framework =
          FrameworkForFile(file, framework_mappings, framework);
      ::std::string imports;
      if (file_framework.empty()) {
        imports = LocalImport(file_name + ".pbrpc.h") +
                  LocalImport(file_name + ".pbobjc.h");
      } else {
        imports = FrameworkImport(file_name + ".pbrpc.h", file_framework) +
                  FrameworkImport(file_name + ".pbobjc.h", file_framework);
      }

      if (grpc_local_import) {
        if (generator_params.no_v1_compatibility) {
          imports +=
              LocalImport(grpc_local_import_prefix + "ProtoRPC/ProtoRPC.h");
        } else {
          imports += LocalImport(grpc_local_import_prefix +
                                 "ProtoRPC/ProtoRPCLegacy.h");
          imports += LocalImport(grpc_local_import_prefix +
                                 "RxLibrary/GRXWriter+Immediate.h");
        }
      } else {
        if (generator_params.no_v1_compatibility) {
          imports += SystemImport("ProtoRPC/ProtoRPC.h");
        } else {
          imports += SystemImport("ProtoRPC/ProtoRPCLegacy.h");
          imports += SystemImport("RxLibrary/GRXWriter+Immediate.h");
        }
      }

      ::std::string class_imports;
      for (int i = 0; i < file->dependency_count(); i++) {
        class_imports +=
            ImportProtoHeaders(file->dependency(i), "", framework_mappings,
                               framework, pb_runtime_import_prefix);
      }

      ::std::string definitions;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        definitions +=
            grpc_objective_c_generator::GetSource(service, generator_params);
      }

      Write(context, file_name + ".pbrpc.m",
            file_header +
                PreprocIfNot(kProtocolOnly, imports + "\n" + class_imports +
                                                "\n" + definitions));
    }

    return true;
  }

 private:
  // Write the given code into the given file.
  void Write(grpc::protobuf::compiler::GeneratorContext* context,
             const ::std::string& filename, const ::std::string& code) const {
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->Open(filename));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
  }
};

int main(int argc, char* argv[]) {
  ObjectiveCGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
