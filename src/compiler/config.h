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

#ifndef SRC_COMPILER_CONFIG_H
#define SRC_COMPILER_CONFIG_H

#include <grpc++/impl/codegen/config_protobuf.h>

#ifndef GRPC_CUSTOM_DESCRIPTOR
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#define GRPC_CUSTOM_DESCRIPTOR ::google::protobuf::Descriptor
#define GRPC_CUSTOM_FILEDESCRIPTOR ::google::protobuf::FileDescriptor
#define GRPC_CUSTOM_FILEDESCRIPTORPROTO ::google::protobuf::FileDescriptorProto
#define GRPC_CUSTOM_METHODDESCRIPTOR ::google::protobuf::MethodDescriptor
#define GRPC_CUSTOM_SERVICEDESCRIPTOR ::google::protobuf::ServiceDescriptor
#define GRPC_CUSTOM_SOURCELOCATION ::google::protobuf::SourceLocation
#endif

#ifndef GRPC_CUSTOM_CODEGENERATOR
#include <google/protobuf/compiler/code_generator.h>
#define GRPC_CUSTOM_CODEGENERATOR ::google::protobuf::compiler::CodeGenerator
#define GRPC_CUSTOM_GENERATORCONTEXT \
  ::google::protobuf::compiler::GeneratorContext
#endif

#ifndef GRPC_CUSTOM_PRINTER
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#define GRPC_CUSTOM_PRINTER ::google::protobuf::io::Printer
#define GRPC_CUSTOM_CODEDOUTPUTSTREAM ::google::protobuf::io::CodedOutputStream
#define GRPC_CUSTOM_STRINGOUTPUTSTREAM \
  ::google::protobuf::io::StringOutputStream
#endif

#ifndef GRPC_CUSTOM_PLUGINMAIN
#include <google/protobuf/compiler/plugin.h>
#define GRPC_CUSTOM_PLUGINMAIN ::google::protobuf::compiler::PluginMain
#endif

#ifndef GRPC_CUSTOM_PARSEGENERATORPARAMETER
#include <google/protobuf/compiler/code_generator.h>
#define GRPC_CUSTOM_PARSEGENERATORPARAMETER ::google::protobuf::compiler::ParseGeneratorParameter
#endif

#ifndef GRPC_CUSTOM_STRING
#include <string>
#define GRPC_CUSTOM_STRING std::string
#endif

namespace grpc {

typedef GRPC_CUSTOM_STRING string;

namespace protobuf {
typedef GRPC_CUSTOM_DESCRIPTOR Descriptor;
typedef GRPC_CUSTOM_FILEDESCRIPTOR FileDescriptor;
typedef GRPC_CUSTOM_FILEDESCRIPTORPROTO FileDescriptorProto;
typedef GRPC_CUSTOM_METHODDESCRIPTOR MethodDescriptor;
typedef GRPC_CUSTOM_SERVICEDESCRIPTOR ServiceDescriptor;
typedef GRPC_CUSTOM_SOURCELOCATION SourceLocation;
namespace compiler {
typedef GRPC_CUSTOM_CODEGENERATOR CodeGenerator;
typedef GRPC_CUSTOM_GENERATORCONTEXT GeneratorContext;
static inline int PluginMain(int argc, char* argv[],
                             const CodeGenerator* generator) {
  return GRPC_CUSTOM_PLUGINMAIN(argc, argv, generator);
}
static inline void ParseGeneratorParameter(const string& parameter,
    std::vector<std::pair<string, string> >* options) {
  GRPC_CUSTOM_PARSEGENERATORPARAMETER(parameter, options);
}

}  // namespace compiler
namespace io {
typedef GRPC_CUSTOM_PRINTER Printer;
typedef GRPC_CUSTOM_CODEDOUTPUTSTREAM CodedOutputStream;
typedef GRPC_CUSTOM_STRINGOUTPUTSTREAM StringOutputStream;
}  // namespace io
}  // namespace protobuf
}  // namespace grpc

#endif  // SRC_COMPILER_CONFIG_H
