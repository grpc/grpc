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
