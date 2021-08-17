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

// Generates Ruby gRPC service interface out of Protobuf IDL.

#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/ruby_generator.h"
#include "src/compiler/ruby_generator_helpers-inl.h"

class RubyGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  RubyGrpcGenerator() {}
  ~RubyGrpcGenerator() {}

  uint64_t GetSupportedFeatures() const override {
    return FEATURE_PROTO3_OPTIONAL;
  }

  bool Generate(const grpc::protobuf::FileDescriptor* file,
                const std::string& /*parameter*/,
                grpc::protobuf::compiler::GeneratorContext* context,
                std::string* /*error*/) const override {
    std::string code = grpc_ruby_generator::GetServices(file);
    if (code.size() == 0) {
      return true;  // don't generate a file if there are no services
    }

    // Get output file name.
    std::string file_name;
    if (!grpc_ruby_generator::ServicesFilename(file, &file_name)) {
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
  RubyGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
