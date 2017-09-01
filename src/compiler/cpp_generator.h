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

#ifndef GRPC_INTERNAL_COMPILER_CPP_GENERATOR_H
#define GRPC_INTERNAL_COMPILER_CPP_GENERATOR_H

// cpp_generator.h/.cc do not directly depend on GRPC/ProtoBuf, such that they
// can be used to generate code for other serialization systems, such as
// FlatBuffers.

#include <memory>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/schema_interface.h"

#ifndef GRPC_CUSTOM_STRING
#include <string>
#define GRPC_CUSTOM_STRING std::string
#endif

namespace grpc {

typedef GRPC_CUSTOM_STRING string;

}  // namespace grpc

namespace grpc_cpp_generator {

// Contains all the parameters that are parsed from the command line.
struct Parameters {
  // Puts the service into a namespace
  grpc::string services_namespace;
  // Use system includes (<>) or local includes ("")
  bool use_system_headers;
  // Prefix to any grpc include
  grpc::string grpc_search_path;
  // Generate GMOCK code to facilitate unit testing.
  bool generate_mock_code;
};

// Return the prologue of the generated header file.
grpc::string GetHeaderPrologue(grpc_generator::File *file,
                               const Parameters &params);

// Return the includes needed for generated header file.
grpc::string GetHeaderIncludes(grpc_generator::File *file,
                               const Parameters &params);

// Return the includes needed for generated source file.
grpc::string GetSourceIncludes(grpc_generator::File *file,
                               const Parameters &params);

// Return the epilogue of the generated header file.
grpc::string GetHeaderEpilogue(grpc_generator::File *file,
                               const Parameters &params);

// Return the prologue of the generated source file.
grpc::string GetSourcePrologue(grpc_generator::File *file,
                               const Parameters &params);

// Return the services for generated header file.
grpc::string GetHeaderServices(grpc_generator::File *file,
                               const Parameters &params);

// Return the services for generated source file.
grpc::string GetSourceServices(grpc_generator::File *file,
                               const Parameters &params);

// Return the epilogue of the generated source file.
grpc::string GetSourceEpilogue(grpc_generator::File *file,
                               const Parameters &params);

// Return the prologue of the generated mock file.
grpc::string GetMockPrologue(grpc_generator::File *file,
                             const Parameters &params);

// Return the includes needed for generated mock file.
grpc::string GetMockIncludes(grpc_generator::File *file,
                             const Parameters &params);

// Return the services for generated mock file.
grpc::string GetMockServices(grpc_generator::File *file,
                             const Parameters &params);

// Return the epilogue of generated mock file.
grpc::string GetMockEpilogue(grpc_generator::File *file,
                             const Parameters &params);

// Return the prologue of the generated mock file.
grpc::string GetMockPrologue(grpc_generator::File *file,
                             const Parameters &params);

// Return the includes needed for generated mock file.
grpc::string GetMockIncludes(grpc_generator::File *file,
                             const Parameters &params);

// Return the services for generated mock file.
grpc::string GetMockServices(grpc_generator::File *file,
                             const Parameters &params);

// Return the epilogue of generated mock file.
grpc::string GetMockEpilogue(grpc_generator::File *file,
                             const Parameters &params);

}  // namespace grpc_cpp_generator

#endif  // GRPC_INTERNAL_COMPILER_CPP_GENERATOR_H
