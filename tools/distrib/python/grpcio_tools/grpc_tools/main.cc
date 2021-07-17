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
#include <google/protobuf/compiler/python/python_generator.h>

#include "src/compiler/python_generator.h"

#include "grpc_tools/main.h"

#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

using ::google::protobuf::FileDescriptor;
using ::google::protobuf::compiler::CodeGenerator;
using ::google::protobuf::compiler::DiskSourceTree;
using ::google::protobuf::compiler::GeneratorContext;
using ::google::protobuf::compiler::Importer;
using ::google::protobuf::compiler::MultiFileErrorCollector;
using ::google::protobuf::io::StringOutputStream;
using ::google::protobuf::io::ZeroCopyOutputStream;

namespace grpc_tools {
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

namespace internal {

class GeneratorContextImpl : public GeneratorContext {
 public:
  GeneratorContextImpl(
      const std::vector<const FileDescriptor*>& parsed_files,
      std::vector<std::pair<std::string, std::string>>* files_out)
      : files_(files_out), parsed_files_(parsed_files) {}

  ZeroCopyOutputStream* Open(const std::string& filename) {
    files_->emplace_back(filename, "");
    return new StringOutputStream(&(files_->back().second));
  }

  // NOTE(rbellevi): Equivalent to Open, since all files start out empty.
  ZeroCopyOutputStream* OpenForAppend(const std::string& filename) {
    return Open(filename);
  }

  // NOTE(rbellevi): Equivalent to Open, since all files start out empty.
  ZeroCopyOutputStream* OpenForInsert(const std::string& filename,
                                      const std::string& insertion_point) {
    return Open(filename);
  }

  void ListParsedFiles(
      std::vector<const ::google::protobuf::FileDescriptor*>* output) {
    *output = parsed_files_;
  }

 private:
  std::vector<std::pair<std::string, std::string>>* files_;
  const std::vector<const FileDescriptor*>& parsed_files_;
};

class ErrorCollectorImpl : public MultiFileErrorCollector {
 public:
  ErrorCollectorImpl(std::vector<::grpc_tools::ProtocError>* errors,
                     std::vector<::grpc_tools::ProtocWarning>* warnings)
      : errors_(errors), warnings_(warnings) {}

  void AddError(const std::string& filename, int line, int column,
                const std::string& message) {
    errors_->emplace_back(filename, line, column, message);
  }

  void AddWarning(const std::string& filename, int line, int column,
                  const std::string& message) {
    warnings_->emplace_back(filename, line, column, message);
  }

 private:
  std::vector<::grpc_tools::ProtocError>* errors_;
  std::vector<::grpc_tools::ProtocWarning>* warnings_;
};

static void calculate_transitive_closure(
    const FileDescriptor* descriptor,
    std::vector<const FileDescriptor*>* transitive_closure,
    std::unordered_set<const ::google::protobuf::FileDescriptor*>* visited) {
  for (int i = 0; i < descriptor->dependency_count(); ++i) {
    const FileDescriptor* dependency = descriptor->dependency(i);
    if (visited->find(dependency) == visited->end()) {
      calculate_transitive_closure(dependency, transitive_closure, visited);
    }
  }
  transitive_closure->push_back(descriptor);
  visited->insert(descriptor);
}

}  // end namespace internal

static int generate_code(
    CodeGenerator* code_generator, char* protobuf_path,
    const std::vector<std::string>* include_paths,
    std::vector<std::pair<std::string, std::string>>* files_out,
    std::vector<::grpc_tools::ProtocError>* errors,
    std::vector<::grpc_tools::ProtocWarning>* warnings) {
  std::unique_ptr<internal::ErrorCollectorImpl> error_collector(
      new internal::ErrorCollectorImpl(errors, warnings));
  std::unique_ptr<DiskSourceTree> source_tree(new DiskSourceTree());
  for (const auto& include_path : *include_paths) {
    source_tree->MapPath("", include_path);
  }
  Importer importer(source_tree.get(), error_collector.get());
  const FileDescriptor* parsed_file = importer.Import(protobuf_path);
  if (parsed_file == nullptr) {
    return 1;
  }
  std::vector<const FileDescriptor*> transitive_closure;
  std::unordered_set<const FileDescriptor*> visited;
  internal::calculate_transitive_closure(parsed_file, &transitive_closure,
                                         &visited);
  internal::GeneratorContextImpl generator_context(transitive_closure,
                                                   files_out);
  std::string error;
  for (const auto descriptor : transitive_closure) {
    code_generator->Generate(descriptor, "", &generator_context, &error);
  }
  return 0;
}

int protoc_get_protos(
    char* protobuf_path, const std::vector<std::string>* include_paths,
    std::vector<std::pair<std::string, std::string>>* files_out,
    std::vector<::grpc_tools::ProtocError>* errors,
    std::vector<::grpc_tools::ProtocWarning>* warnings) {
  ::google::protobuf::compiler::python::Generator python_generator;
  return generate_code(&python_generator, protobuf_path, include_paths,
                       files_out, errors, warnings);
}

int protoc_get_services(
    char* protobuf_path, const std::vector<std::string>* include_paths,
    std::vector<std::pair<std::string, std::string>>* files_out,
    std::vector<::grpc_tools::ProtocError>* errors,
    std::vector<::grpc_tools::ProtocWarning>* warnings) {
  grpc_python_generator::GeneratorConfiguration grpc_py_config;
  grpc_python_generator::PythonGrpcGenerator grpc_py_generator(grpc_py_config);
  return generate_code(&grpc_py_generator, protobuf_path, include_paths,
                       files_out, errors, warnings);
}
}  // end namespace grpc_tools
