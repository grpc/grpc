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

#ifndef SRC_COMPILER_CONFIG_H
#define SRC_COMPILER_CONFIG_H

#include <grpc++/impl/codegen/config_protobuf.h>

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
#define GRPC_CUSTOM_PARSEGENERATORPARAMETER \
  ::google::protobuf::compiler::ParseGeneratorParameter
#endif

#ifndef GRPC_CUSTOM_STRING
#include <string>
#define GRPC_CUSTOM_STRING std::string
#endif

namespace grpc {

typedef GRPC_CUSTOM_STRING string;

namespace protobuf {

namespace compiler {
typedef GRPC_CUSTOM_CODEGENERATOR CodeGenerator;
typedef GRPC_CUSTOM_GENERATORCONTEXT GeneratorContext;
static inline int PluginMain(int argc, char* argv[],
                             const CodeGenerator* generator) {
  return GRPC_CUSTOM_PLUGINMAIN(argc, argv, generator);
}
static inline void ParseGeneratorParameter(
    const string& parameter, std::vector<std::pair<string, string> >* options) {
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

namespace grpc_cpp_generator {

static const char* const kCppGeneratorMessageHeaderExt = ".pb.h";
static const char* const kCppGeneratorServiceHeaderExt = ".grpc.pb.h";

}  // namespace grpc_cpp_generator

#endif  // SRC_COMPILER_CONFIG_H
