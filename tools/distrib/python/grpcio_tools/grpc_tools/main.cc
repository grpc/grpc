// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <google/protobuf/compiler/command_line_interface.h>
#include <google/protobuf/compiler/command_line_interface.h>
#include <google/protobuf/compiler/python/python_generator.h>

#include "src/compiler/python_generator.h"

#include "grpc_tools/main.h"

#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>

// TODO: Clang format.
#include <vector>
#include <map>
#include <string>
#include <tuple>

int protoc_main(int argc, char* argv[]) {
  google::protobuf::compiler::CommandLineInterface cli;
  cli.AllowPlugins("protoc-");

  // Proto2 Python
  google::protobuf::compiler::python::Generator py_generator;
  cli.RegisterGenerator("--python_out", &py_generator,
                        "Generate Python source file.");

  // gRPC Python
  grpc_python_generator::GeneratorConfiguration grpc_py_config;
  grpc_python_generator::PythonGrpcGenerator grpc_py_generator(grpc_py_config);
  cli.RegisterGenerator("--grpc_python_out", &grpc_py_generator,
                        "Generate Python source file.");

  return cli.Run(argc, argv);
}

// TODO: Figure out what Google best practices are for internal namespace like
// this.
namespace detail {

// TODO: Consider deduping between this and command_line_interface.cc.
// TODO: Separate declarations and definitions.
class GeneratorContextImpl : public ::google::protobuf::compiler::GeneratorContext {
public:
  GeneratorContextImpl(const std::vector<const ::google::protobuf::FileDescriptor*>& parsed_files) :
    parsed_files_(parsed_files){}

  ::google::protobuf::io::ZeroCopyOutputStream* Open(const std::string& filename) {
    // TODO(rbellevi): Learn not to dream impossible dreams. :(
    auto [iter, _] = files_.emplace(filename, "");
    return new ::google::protobuf::io::StringOutputStream(&(iter->second));
  }

  // NOTE: Equivalent to Open, since all files start out empty.
  ::google::protobuf::io::ZeroCopyOutputStream* OpenForAppend(const std::string& filename) {
    return Open(filename);
  }

  // NOTE: Equivalent to Open, since all files start out empty.
  ::google::protobuf::io::ZeroCopyOutputStream* OpenForInsert(
      const std::string& filename, const std::string& insertion_point) {
    return Open(filename);
  }

  void ListParsedFiles(std::vector<const ::google::protobuf::FileDescriptor*>* output) {
    *output = parsed_files_;
  }

  // TODO: Figure out a method with less copying.
  std::map<std::string, std::string>
  GetFiles() const {
    return files_;
  }

private:
  std::map<std::string, std::string> files_;
  const std::vector<const ::google::protobuf::FileDescriptor*>& parsed_files_;
};

class ErrorCollectorImpl : public ::google::protobuf::compiler::MultiFileErrorCollector {
 public:
  ErrorCollectorImpl() {}
  ~ErrorCollectorImpl() {}

  // implements ErrorCollector ---------------------------------------
  void AddError(const std::string& filename, int line, int column,
                const std::string& message) {
    // TODO: Implement.
  }

  void AddWarning(const std::string& filename, int line, int column,
                  const std::string& message) {
    // TODO: Implement.
  }
};

} // end namespace detail

#include <iostream>

int protoc_in_memory(char* protobuf_path, char* include_path) {
  std::cout << "C++ protoc_in_memory" << std::endl << std::flush;
  // TODO: Create parsed_files.
  std::string protobuf_filename(protobuf_path);
  std::unique_ptr<detail::ErrorCollectorImpl> error_collector(new detail::ErrorCollectorImpl());
  std::unique_ptr<::google::protobuf::compiler::DiskSourceTree> source_tree(new ::google::protobuf::compiler::DiskSourceTree());
  // NOTE: This is equivalent to "--proto_path=."
  source_tree->MapPath("", ".");
  // TODO: Figure out more advanced virtual path mapping.
  ::google::protobuf::compiler::Importer importer(source_tree.get(), error_collector.get());
  const ::google::protobuf::FileDescriptor* parsed_file = importer.Import(protobuf_filename);
  detail::GeneratorContextImpl generator_context({parsed_file});
  std::string error;
  ::google::protobuf::compiler::python::Generator python_generator;
  python_generator.Generate(parsed_file, "", &generator_context, &error);
  for (const auto& [filename, contents] : generator_context.GetFiles()) {
    std::cout << "# File: " << filename << std::endl;
    std::cout << contents << std::endl;
    std::cout << std::endl;
  }
  std::cout << std::flush;
  // TODO: Come up with a better error reporting mechanism than this.
  return 0;
}
