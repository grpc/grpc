/*
 *
 * Copyright 2016 gRPC authors.
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

// Generates PHP gRPC service interface out of Protobuf IDL.

#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/php_generator.h"
#include "src/compiler/php_generator_helpers.h"

using google::protobuf::compiler::ParseGeneratorParameter;
using grpc_php_generator::GenerateFile;
using grpc_php_generator::GetPHPServiceFilename;

class PHPGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  PHPGrpcGenerator() {}
  ~PHPGrpcGenerator() {}

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
    if (file->service_count() == 0) {
      return true;
    }

    std::vector<std::pair<std::string, std::string> > options;
    ParseGeneratorParameter(parameter, &options);

    bool generate_server = false;
    std::string class_suffix;
    for (size_t i = 0; i < options.size(); ++i) {
      if (options[i].first == "class_suffix") {
        class_suffix = options[i].second;
      } else if (options[i].first == "generate_server") {
        generate_server = true;
      } else {
        *error = "unsupported options: " + options[i].first;
        return false;
      }
    }

    for (int i = 0; i < file->service_count(); i++) {
      GenerateService(file, file->service(i), class_suffix, false, context);
      if (generate_server) {
        GenerateService(file, file->service(i), class_suffix, true, context);
      }
    }

    return true;
  }

 private:
  void GenerateService(
      const grpc::protobuf::FileDescriptor* file,
      const grpc::protobuf::ServiceDescriptor* service,
      const std::string& class_suffix, bool is_server,
      grpc::protobuf::compiler::GeneratorContext* context) const {
    std::string code = GenerateFile(file, service, class_suffix, is_server);

    // Get output file name
    std::string file_name =
        GetPHPServiceFilename(file, service, class_suffix, is_server);

    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->Open(file_name));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
  }
};

int main(int argc, char* argv[]) {
  PHPGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
