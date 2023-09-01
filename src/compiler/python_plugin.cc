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

// Generates a Python gRPC service interface out of Protobuf IDL.

#include "src/compiler/config.h"
#include "src/compiler/protobuf_plugin.h"
#include "src/compiler/python_generator.h"

int main(int argc, char* argv[]) {
  grpc_python_generator::GeneratorConfiguration config;
  grpc_python_generator::PythonGrpcGenerator generator(config);
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
