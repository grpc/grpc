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

  bool Generate(const grpc::protobuf::FileDescriptor *file,
                const grpc::string &parameter,
                grpc::protobuf::compiler::GeneratorContext *context,
                grpc::string *error) const {
    std::vector<std::pair<grpc::string, grpc::string> > options;
    grpc::protobuf::compiler::ParseGeneratorParameter(parameter, &options);

    bool generate_client = true;
    bool generate_server = true;
    bool internal_access = false;
    for (size_t i = 0; i < options.size(); i++) {
      if (options[i].first == "no_client") {
        generate_client = false;
      } else if (options[i].first == "no_server") {
        generate_server = false;
      } else if (options[i].first == "internal_access") {
        internal_access = true;
      } else {
        *error = "Unknown generator option: " + options[i].first;
        return false;
      }
    }

    grpc::string code = grpc_csharp_generator::GetServices(
        file, generate_client, generate_server, internal_access);
    if (code.size() == 0) {
      return true;  // don't generate a file if there are no services
    }

    // Get output file name.
    grpc::string file_name;
    if (!grpc_csharp_generator::ServicesFilename(file, &file_name)) {
      return false;
    }
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->Open(file_name));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
    return true;
  }
};

int main(int argc, char *argv[]) {
  CSharpGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
