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

using ::google::protobuf::compiler::objectivec::
    IsProtobufLibraryBundledProtoFile;
using ::google::protobuf::compiler::objectivec::ProtobufLibraryFrameworkName;
using ::grpc_objective_c_generator::LocalImport;
using ::grpc_objective_c_generator::PreprocIfElse;
using ::grpc_objective_c_generator::PreprocIfNot;
using ::grpc_objective_c_generator::SystemImport;

namespace {

inline ::grpc::string ImportProtoHeaders(
    const grpc::protobuf::FileDescriptor* dep, const char* indent) {
  ::grpc::string header = grpc_objective_c_generator::MessageHeaderName(dep);

  if (!IsProtobufLibraryBundledProtoFile(dep)) {
    return indent + LocalImport(header);
  }

  ::grpc::string base_name = header;
  grpc_generator::StripPrefix(&base_name, "google/protobuf/");
  // create the import code snippet
  ::grpc::string framework_header =
      ::grpc::string(ProtobufLibraryFrameworkName) + "/" + base_name;

  static const ::grpc::string kFrameworkImportsCondition =
      "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS";
  return PreprocIfElse(kFrameworkImportsCondition,
                       indent + SystemImport(framework_header),
                       indent + LocalImport(header));
}

}  // namespace

class ObjectiveCGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  ObjectiveCGrpcGenerator() {}
  virtual ~ObjectiveCGrpcGenerator() {}

 public:
  virtual bool Generate(const grpc::protobuf::FileDescriptor* file,
                        const ::grpc::string& parameter,
                        grpc::protobuf::compiler::GeneratorContext* context,
                        ::grpc::string* error) const {
    if (file->service_count() == 0) {
      // No services.  Do nothing.
      return true;
    }

    static const ::grpc::string kNonNullBegin = "NS_ASSUME_NONNULL_BEGIN\n";
    static const ::grpc::string kNonNullEnd = "NS_ASSUME_NONNULL_END\n";
    static const ::grpc::string kProtocolOnly = "GPB_GRPC_PROTOCOL_ONLY";
    static const ::grpc::string kForwardDeclare =
        "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO";

    ::grpc::string file_name =
        google::protobuf::compiler::objectivec::FilePath(file);

    {
      // Generate .pbrpc.h

      ::grpc::string imports = LocalImport(file_name + ".pbobjc.h");

      ::grpc::string system_imports = SystemImport("ProtoRPC/ProtoService.h") +
                                      SystemImport("ProtoRPC/ProtoRPC.h") +
                                      SystemImport("RxLibrary/GRXWriteable.h") +
                                      SystemImport("RxLibrary/GRXWriter.h");

      ::grpc::string forward_declarations = "@class GRPCProtoCall;\n\n";

      ::grpc::string class_declarations =
          grpc_objective_c_generator::GetAllMessageClasses(file);

      ::grpc::string class_imports;
      for (int i = 0; i < file->dependency_count(); i++) {
        class_imports += ImportProtoHeaders(file->dependency(i), "  ");
      }

      ::grpc::string protocols;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        protocols += grpc_objective_c_generator::GetProtocol(service);
      }

      ::grpc::string interfaces;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        interfaces += grpc_objective_c_generator::GetInterface(service);
      }

      Write(context, file_name + ".pbrpc.h",
            PreprocIfNot(kForwardDeclare, imports) + "\n" +
                PreprocIfNot(kProtocolOnly, system_imports) + "\n" +
                class_declarations + "\n" +
                PreprocIfNot(kForwardDeclare, class_imports) + "\n" +
                forward_declarations + "\n" + kNonNullBegin + "\n" + protocols +
                "\n" + PreprocIfNot(kProtocolOnly, interfaces) + "\n" +
                kNonNullEnd + "\n");
    }

    {
      // Generate .pbrpc.m

      ::grpc::string imports = LocalImport(file_name + ".pbrpc.h") +
                               LocalImport(file_name + ".pbobjc.h") +
                               SystemImport("ProtoRPC/ProtoRPC.h") +
                               SystemImport("RxLibrary/GRXWriter+Immediate.h");

      ::grpc::string class_imports;
      for (int i = 0; i < file->dependency_count(); i++) {
        class_imports += ImportProtoHeaders(file->dependency(i), "");
      }

      ::grpc::string definitions;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor* service = file->service(i);
        definitions += grpc_objective_c_generator::GetSource(service);
      }

      Write(context, file_name + ".pbrpc.m",
            PreprocIfNot(kProtocolOnly,
                         imports + "\n" + class_imports + "\n" + definitions));
    }

    return true;
  }

 private:
  // Write the given code into the given file.
  void Write(grpc::protobuf::compiler::GeneratorContext* context,
             const ::grpc::string& filename, const ::grpc::string& code) const {
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
