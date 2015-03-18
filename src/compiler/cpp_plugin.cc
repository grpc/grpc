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

// Generates cpp gRPC service interface out of Protobuf IDL.
//

#include <memory>

#include "src/compiler/config.h"

#include "src/compiler/cpp_generator.h"
#include "src/compiler/cpp_generator_helpers.h"

class CppGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  CppGrpcGenerator() {}
  virtual ~CppGrpcGenerator() {}

  virtual bool Generate(const grpc::protobuf::FileDescriptor *file,
                        const grpc::string &parameter,
                        grpc::protobuf::compiler::GeneratorContext *context,
                        grpc::string *error) const {
    if (file->options().cc_generic_services()) {
      *error =
          "cpp grpc proto compiler plugin does not work with generic "
          "services. To generate cpp grpc APIs, please set \""
          "cc_generic_service = false\".";
      return false;
    }

    grpc::string file_name = grpc_generator::StripProto(file->name());

    // Generate .pb.h
    Insert(context, file_name + ".pb.h", "includes",
           grpc_cpp_generator::GetHeaderIncludes(file));
    Insert(context, file_name + ".pb.h", "namespace_scope",
           grpc_cpp_generator::GetHeaderServices(file));
    // Generate .pb.cc
    Insert(context, file_name + ".pb.cc", "includes",
           grpc_cpp_generator::GetSourceIncludes());
    Insert(context, file_name + ".pb.cc", "namespace_scope",
           grpc_cpp_generator::GetSourceServices(file));

    return true;
  }

 private:
  // Insert the given code into the given file at the given insertion point.
  void Insert(grpc::protobuf::compiler::GeneratorContext *context,
              const grpc::string &filename, const grpc::string &insertion_point,
              const grpc::string &code) const {
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->OpenForInsert(filename, insertion_point));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
  }
};

int main(int argc, char *argv[]) {
  CppGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
