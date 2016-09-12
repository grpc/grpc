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

#ifndef GRPC_INTERNAL_COMPILER_PYTHON_GENERATOR_HELPERS_H
#define GRPC_INTERNAL_COMPILER_PYTHON_GENERATOR_HELPERS_H

#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/python_generator.h"
#include "src/compiler/protobuf_plugin.h"

namespace grpc_python_generator {
   GeneratorConfiguration::GeneratorConfiguration()
  	: grpc_package_root("grpc"), beta_package_root("grpc.beta") {}

   PythonGrpcGenerator::PythonGrpcGenerator(const GeneratorConfiguration& config)
  	: config_(config) {}

   PythonGrpcGenerator::~PythonGrpcGenerator() {}

   bool PythonGrpcGenerator::Generate(const grpc::protobuf::FileDescriptor* file,
  	                                const grpc::string& parameter,
  	                                grpc::protobuf::compiler::GeneratorContext* context,
  	                                grpc::string* error) const {
   // Get output file name.
   grpc::string file_name;

   ProtoBufFile pbfile(file);

   static const int proto_suffix_length = strlen(".proto");
   if (file->name().size() > static_cast<size_t>(proto_suffix_length) &&
  	     file->name().find_last_of(".proto") == file->name().size() - 1) {
  	   file_name =
  	     file->name().substr(0, file->name().size() - proto_suffix_length) +
  	     "_pb2.py";
  	   } else {
  	     *error = "Invalid proto file name. Proto file must end with .proto";
  	     return false;
  	   }

  	 std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
  	   context->OpenForInsert(file_name, "module_scope"));
  	 grpc::protobuf::io::CodedOutputStream coded_out(output.get());
  	 bool success = false;
  	 grpc::string code = "";
  	 tie(success, code) = grpc_python_generator::GetServices(&pbfile, config_);
  	 if (success) {
  	   coded_out.WriteRaw(code.data(), code.size());
  	   return true;
  	 } else {
  	   return false;
  	 }
  	}
} // namespace grpc_python_generator 

#endif  // GRPC_INTERNAL_COMPILER_PYTHON_GENERATOR_HELPERS_H
