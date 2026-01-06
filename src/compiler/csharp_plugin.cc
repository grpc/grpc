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

// Generates C# gRPC service interface out of Protobuf IDL.

#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/csharp_generator.h"
#include "src/compiler/csharp_generator_helpers.h"

class CSharpGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  CSharpGrpcGenerator() {}
  ~CSharpGrpcGenerator() {}

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
    return grpc::protobuf::Edition::EDITION_2023;
  }
#endif

  bool Generate(const grpc::protobuf::FileDescriptor* file,
                const std::string& parameter,
                grpc::protobuf::compiler::GeneratorContext* context,
                std::string* error) const override {
    std::vector<std::pair<std::string, std::string> > options;
    grpc::protobuf::compiler::ParseGeneratorParameter(parameter, &options);

    bool generate_client = true;
    bool generate_server = true;
    bool internal_access = false;
    std::string base_namespace = "";
    bool base_namespace_present = false;

    // the suffix that will get appended to the name generated from the name
    // of the original .proto file
    std::string file_suffix = "Grpc.cs";
    for (size_t i = 0; i < options.size(); i++) {
      if (options[i].first == "no_client") {
        generate_client = false;
      } else if (options[i].first == "no_server") {
        generate_server = false;
      } else if (options[i].first == "internal_access") {
        internal_access = true;
      } else if (options[i].first == "file_suffix") {
        file_suffix = options[i].second;
      } else if (options[i].first == "base_namespace") {
        // Support for base_namespace option in this plugin is experimental.
        // The option may be removed or file names generated may change
        // in the future.
        base_namespace = options[i].second;
        base_namespace_present = true;
      } else {
        *error = "Unknown generator option: " + options[i].first;
        return false;
      }
    }

    std::string code = grpc_csharp_generator::GetServices(
        file, generate_client, generate_server, internal_access);
    if (code.size() == 0) {
      return true;  // don't generate a file if there are no services
    }

    // Get output file name.
    std::string file_name;
    if (!grpc_csharp_generator::ServicesFilename(
            file, file_suffix, base_namespace_present, base_namespace,
            file_name, error)) {
      return false;
    }
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->Open(file_name));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
    return true;
  }
};

int main(int argc, char* argv[]) {
  CSharpGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
