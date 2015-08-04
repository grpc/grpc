/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

// Generates Objective C gRPC service interface out of Protobuf IDL.

#include <memory>

#include "src/compiler/config.h"
#include "src/compiler/objective_c_generator.h"
#include "src/compiler/objective_c_generator_helpers.h"

using ::grpc::string;

class ObjectiveCGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  ObjectiveCGrpcGenerator() {}
  virtual ~ObjectiveCGrpcGenerator() {}

  virtual bool Generate(const grpc::protobuf::FileDescriptor *file,
                        const string &parameter,
                        grpc::protobuf::compiler::GeneratorContext *context,
                        string *error) const {

    if (file->service_count() == 0) {
      // No services.  Do nothing.
      return true;
    }

    string file_name = grpc_generator::FileNameInUpperCamel(file);
    string prefix = file->options().objc_class_prefix();

    {
      // Generate .pbrpc.h

      string imports = string("#import \"") + file_name + ".pbobjc.h\"\n\n"
        "#import <ProtoRPC/ProtoService.h>\n"
        "#import <RxLibrary/GRXWriteable.h>\n"
        "#import <RxLibrary/GRXWriter.h>\n";

      // TODO(jcanizales): Instead forward-declare the input and output types
      // and import the files in the .pbrpc.m
      string proto_imports;
      for (int i = 0; i < file->dependency_count(); i++) {
        string header = grpc_objective_c_generator::MessageHeaderName(
            file->dependency(i));
        proto_imports += string("#import \"") + header + "\"\n";
      }

      string declarations;
      for (int i = 0; i < file->service_count(); i++) {
        const grpc::protobuf::ServiceDescriptor *service = file->service(i);
        declarations += grpc_objective_c_generator::GetHeader(service);
      }

      Write(context, file_name + ".pbrpc.h",
          imports + '\n' + proto_imports + '\n' + declarations);
    }

    {
      // Generate .pbrpc.m

      string imports = string("#import \"") + file_name + ".pbrpc.h\"\n\n"
        "#import <ProtoRPC/ProtoRPC.h>\n"
        "#import <RxLibrary/GRXWriter+Immediate.h>\n";

      string definitions;
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
              const string &filename, const string &code) const {
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
