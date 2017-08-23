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

#ifndef GRPCXX_SUPPORT_COMPRESSION_H
#define GRPCXX_SUPPORT_COMPRESSION_H

#include <grpc++/support/status.h>
#include <grpc/compression.h>

namespace grpc {

/// Parse \a level_name (one of "none", "low", "medium", "high") into \a level.
grpc::Status ParseCompressionLevel(const grpc::string& level_name,
                                   grpc_compression_level* level);

/// Parse \a algorithm_name (one of "identity", "gzip", "deflate") into \a
/// algorithm.
grpc::Status ParseCompressionAlgorithm(const grpc::string& algorithm_name,
                                       grpc_compression_algorithm* algorithm);

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_COMPRESSION_H
