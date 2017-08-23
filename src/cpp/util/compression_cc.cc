/*
 *
 * Copyright 2017 gRPC authors.
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

#include <sstream>

#include <grpc++/support/compression.h>

namespace grpc {

grpc::Status ParseCompressionLevel(const grpc::string& level_name,
                                   grpc_compression_level* level) {
  if (grpc_compression_level_parse(level_name.c_str(), level)) {
    return grpc::Status::OK;
  } else {
    std::ostringstream oss;
    oss << "Unknown compression level '" << level_name << "'.";
    return grpc::Status(INVALID_ARGUMENT, oss.str());
  }
}

grpc::Status ParseCompressionAlgorithm(const grpc::string& algorithm_name,
                                       grpc_compression_algorithm* algorithm) {
  if (grpc_compression_algorithm_parse(
          grpc_slice_from_static_string(algorithm_name.c_str()), algorithm)) {
    return grpc::Status::OK;
  } else {
    std::ostringstream oss;
    oss << "Unknown compression algorithm '" << algorithm_name << "'.";
    return grpc::Status(INVALID_ARGUMENT, oss.str());
  }
}

}  // namespace grpc
