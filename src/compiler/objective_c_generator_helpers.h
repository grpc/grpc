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

#ifndef GRPC_INTERNAL_COMPILER_OBJECTIVE_C_GENERATOR_HELPERS_H
#define GRPC_INTERNAL_COMPILER_OBJECTIVE_C_GENERATOR_HELPERS_H

#include <map>
#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"

namespace grpc_objective_c_generator {

using ::grpc::protobuf::FileDescriptor;
using ::grpc::protobuf::ServiceDescriptor;
using ::grpc::string;

inline string MessageHeaderName(const FileDescriptor *file) {
  return grpc_generator::FileNameInUpperCamel(file) + ".pbobjc.h";
}

inline string ServiceClassName(const ServiceDescriptor *service) {
  const FileDescriptor *file = service->file();
  string prefix = file->options().objc_class_prefix();
  return prefix + service->name();
}
}
#endif  // GRPC_INTERNAL_COMPILER_OBJECTIVE_C_GENERATOR_HELPERS_H
