/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_INTERNAL_COMPILER_OBJECTIVE_C_GENERATOR_H
#define GRPC_INTERNAL_COMPILER_OBJECTIVE_C_GENERATOR_H

#include "src/compiler/config.h"

namespace grpc_objective_c_generator {

using ::grpc::protobuf::ServiceDescriptor;
using ::grpc::string;

// Returns the content to be included in the "global_scope" insertion point of
// the generated header file.
string GetHeader(const ServiceDescriptor *service);

// Returns the content to be included in the "global_scope" insertion point of
// the generated implementation file.
string GetSource(const ServiceDescriptor *service);

}  // namespace grpc_objective_c_generator

#endif  // GRPC_INTERNAL_COMPILER_OBJECTIVE_C_GENERATOR_H
