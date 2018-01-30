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

#ifndef GRPCXX_IMPL_CODEGEN_COMPRESSION_H
#define GRPCXX_IMPL_CODEGEN_COMPRESSION_H

#include <grpc/impl/codegen/compression_types.h>

namespace grpc {
class CompressionAlgorithm {
 public:
  enum Algorithm {
    NONE = 0,
    MESSAGE_DEFLATE = 1,
    MESSAGE_GZIP = 2,
    STREAM_GZIP = 3,
    COUNT
  };

  CompressionAlgorithm() : algorithm_(GRPC_COMPRESS_NONE) {}

  CompressionAlgorithm(Algorithm alg)
      : algorithm_(static_cast<grpc_compression_algorithm>(alg)) {
    static_assert(NONE == static_cast<Algorithm>(GRPC_COMPRESS_NONE),
                  "gRPC Core/C++ Algorithm count mismatch");
    static_assert(MESSAGE_DEFLATE ==
                      static_cast<Algorithm>(GRPC_COMPRESS_MESSAGE_DEFLATE),
                  "gRPC Core/C++ Algorithm count mismatch");
    static_assert(
        MESSAGE_GZIP == static_cast<Algorithm>(GRPC_COMPRESS_MESSAGE_GZIP),
        "gRPC Core/C++ Algorithm count mismatch");
    static_assert(
        STREAM_GZIP == static_cast<Algorithm>(GRPC_COMPRESS_STREAM_GZIP),
        "gRPC Core/C++ Algorithm count mismatch");
    static_assert(
        COUNT == static_cast<Algorithm>(GRPC_COMPRESS_ALGORITHMS_COUNT),
        "gRPC Core/C++ Algorithm count mismatch");
  }
  CompressionAlgorithm(grpc_compression_algorithm alg) : algorithm_(alg) {}

  CompressionAlgorithm(const CompressionAlgorithm& alg)
      : algorithm_(alg.algorithm_) {}

  CompressionAlgorithm(CompressionAlgorithm&& alg)
      : algorithm_(alg.algorithm_) {}

  CompressionAlgorithm& operator=(const CompressionAlgorithm& alg) {
    algorithm_ = alg.algorithm_;
    return *this;
  }

  CompressionAlgorithm& operator=(CompressionAlgorithm&& alg) {
    algorithm_ = alg.algorithm_;
    return *this;
  }

  operator grpc_compression_algorithm() const { return algorithm_; }

 private:
  grpc_compression_algorithm algorithm_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_COMPRESSION_H
