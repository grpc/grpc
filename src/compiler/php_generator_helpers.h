/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_INTERNAL_COMPILER_PHP_GENERATOR_HELPERS_H
#define GRPC_INTERNAL_COMPILER_PHP_GENERATOR_HELPERS_H

#include <algorithm>

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"

namespace grpc_php_generator {

inline grpc::string GetPHPServiceFilename(
    const grpc::protobuf::FileDescriptor *file,
    const grpc::protobuf::ServiceDescriptor *service) {
  std::vector<grpc::string> tokens =
      grpc_generator::tokenize(file->package(), ".");
  std::ostringstream oss;
  for (unsigned int i = 0; i < tokens.size(); i++) {
    oss << (i == 0 ? "" : "/")
        << grpc_generator::CapitalizeFirstLetter(tokens[i]);
  }
  return oss.str() + "/" + service->name() + "Client.php";
}

// Get leading or trailing comments in a string. Comment lines start with "// ".
// Leading detached comments are put in in front of leading comments.
template <typename DescriptorType>
inline grpc::string GetPHPComments(const DescriptorType *desc,
                                   grpc::string prefix) {
  return grpc_generator::GetPrefixedComments(desc, true, prefix);
}

}  // namespace grpc_php_generator

#endif  // GRPC_INTERNAL_COMPILER_PHP_GENERATOR_HELPERS_H
