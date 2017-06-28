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

#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/objective_c_generator.h"
#include "src/compiler/objective_c_generator_helpers.h"

#include <google/protobuf/compiler/objectivec/objectivec_helpers.h>

using ::google::protobuf::compiler::objectivec::ProtobufLibraryFrameworkName;
using ::google::protobuf::compiler::objectivec::
    IsProtobufLibraryBundledProtoFile;

class ObjectiveCGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  ObjectiveCGrpcGenerator() {}
  virtual ~ObjectiveCGrpcGenerator() {}

  virtual bool Generate(const grpc::protobuf::FileDescriptor *file,
                        const ::grpc::string &parameter,
                        grpc::protobuf::compiler::GeneratorContext *context,
                        ::grpc::string *error) const {
    if (file->service_count() == 0) {
      // No services.  Do nothing.
      return true;
    }

    ::grpc::string file_name =
        google::protobuf::compiler::objectivec::FilePath(file);
    ::grpc::string prefix = file->options().objc_class_prefix();

    {
      // Generate .pbrpc.h

      ::grpc::string imports = ::grpc::string("#import \"") + file_name +
                               ".pbobjc.h\"\n\n"
                               "#import <ProtoRPC/ProtoService.h>\n"
                               "#import <ProtoRPC/ProtoRPC.h>\n"
                               "#import <RxLibrary/GRXWriteable.h>\n"
                               "#import <RxLibrary/GRXWriter.h>\n";

      // TODO(jcanizales): Instead forward-declare the input and output types
      // and import the files in the .pbrpc.m
      ::grpc::string proto_imports;
      for (int i = 0; i < file->dependency_count(); i++) {
        ::grpc::string header =
            grpc_objective_c_generator::MessageHeaderName(file->dependency(i));
        const grpc::protobuf::FileDescriptor *dependency = file->dependency(i);
        if (IsProtobufLibraryBundledProtoFile(dependency)) {
          ::grpc::string base_name = header;
          grpc_generator::StripPrefix(&base_name, "google/protobuf/");
          // create the import code snippet
          proto_imports +=
              "#if GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS\n"
              "  #import <" +
              ::grpc::string(ProtobufLibraryFrameworkName) + "/" + base_name +
              ">\n"
              "#else\n"
              "  #import \"" +
              header +
              "\"\n"
              "#endif\n";
        } else {
          proto_imports += ::grpc::string("#import \"") + header + "\"\n";
        }
      }

      ::grpc::string declarations;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor *service = file->service(i);
        declarations += grpc_objective_c_generator::GetHeader(service);
      }

      static const ::grpc::string kNonNullBegin =
          "\nNS_ASSUME_NONNULL_BEGIN\n\n";
      static const ::grpc::string kNonNullEnd = "\nNS_ASSUME_NONNULL_END\n";

      Write(context, file_name + ".pbrpc.h", imports + '\n' + proto_imports +
                                                 '\n' + kNonNullBegin +
                                                 declarations + kNonNullEnd);
    }

    {
      // Generate .pbrpc.m

      ::grpc::string imports = ::grpc::string("#import \"") + file_name +
                               ".pbrpc.h\"\n\n"
                               "#import <ProtoRPC/ProtoRPC.h>\n"
                               "#import <RxLibrary/GRXWriter+Immediate.h>\n";

      ::grpc::string definitions;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor *service = file->service(i);
        definitions += grpc_objective_c_generator::GetSource(service);
      }

      Write(context, file_name + ".pbrpc.m", imports + '\n' + definitions);
    }

    return true;
  }

 private:
  // Write the given code into the given file.
  void Write(grpc::protobuf::compiler::GeneratorContext *context,
             const ::grpc::string &filename, const ::grpc::string &code) const {
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->Open(filename));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
  }
};

int main(int argc, char *argv[]) {
  ObjectiveCGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
