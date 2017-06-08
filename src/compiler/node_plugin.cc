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

// Generates Node gRPC service interface out of Protobuf IDL.

#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/node_generator.h"
#include "src/compiler/node_generator_helpers.h"

using grpc_node_generator::GenerateFile;
using grpc_node_generator::GetJSServiceFilename;

class NodeGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  NodeGrpcGenerator() {}
  ~NodeGrpcGenerator() {}

  bool Generate(const grpc::protobuf::FileDescriptor *file,
                const grpc::string &parameter,
                grpc::protobuf::compiler::GeneratorContext *context,
                grpc::string *error) const {
    grpc::string code = GenerateFile(file);
    if (code.size() == 0) {
      return true;
    }

    // Get output file name
    grpc::string file_name = GetJSServiceFilename(file->name());

    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->Open(file_name));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
    return true;
  }
};

int main(int argc, char *argv[]) {
  NodeGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
