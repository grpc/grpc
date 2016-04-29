#include <google/protobuf/compiler/command_line_interface.h>
#include <google/protobuf/compiler/python/python_generator.h>

#include "src/compiler/python_generator.h"

#include "grpc/protoc/main.h"

int main(int argc, char* argv[]) {
  google::protobuf::compiler::CommandLineInterface cli;
  cli.AllowPlugins("protoc-");

  // Proto2 Python
  google::protobuf::compiler::python::Generator py_generator;
  cli.RegisterGenerator("--python_out", &py_generator,
                        "Generate Python source file.");

  // gRPC Python
  grpc_python_generator::GeneratorConfiguration grpc_py_config;
  grpc_py_config.beta_package_root = "grpc.beta";
  grpc_python_generator::PythonGrpcGenerator grpc_py_generator(grpc_py_config);
  cli.RegisterGenerator("--grpc_python_out", &grpc_py_generator,
                        "Generate Python source file.");

  return cli.Run(argc, argv);
}
