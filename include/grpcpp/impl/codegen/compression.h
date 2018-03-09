/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_COMPRESSION_H
#define GRPCPP_IMPL_CODEGEN_COMPRESSION_H

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
                  "gRPC Core/C++ compression algorithm value mismatch");
    static_assert(
        MESSAGE_DEFLATE == static_cast<Algorithm>(GRPC_COMPRESS_DEFLATE),
        "gRPC Core/C++ compression algorithm value mismatch");
    static_assert(MESSAGE_GZIP == static_cast<Algorithm>(GRPC_COMPRESS_GZIP),
                  "gRPC Core/C++ compression algorithm value mismatch");
    static_assert(
        STREAM_GZIP == static_cast<Algorithm>(GRPC_COMPRESS_STREAM_GZIP),
        "gRPC Core/C++ compression algorithm value mismatch");
    static_assert(
        COUNT == static_cast<Algorithm>(GRPC_COMPRESS_ALGORITHMS_COUNT),
        "gRPC Core/C++ compression algorithm value mismatch");
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

  /// Provide a conversion to Algorithm to support comparison against a
  /// specific compression algorithm
  operator Algorithm() const { return static_cast<Algorithm>(algorithm_); }

  /// This conversion operator is needed to maintain API compatibility
  /// with older release versions of the C++ API that returned
  /// grpc_compression_algorithm from ClientContext and ServerContext
  /// rather than grpc::CompressionAlgorithm
  operator grpc_compression_algorithm() const { return algorithm_; }

  /// Having 2 ambiguous enum conversions is ambiguous for gtest which needs
  /// to print it as an integral type, so provide a general integral conversion
  template <class T>
  operator T() const {
    static_assert(std::is_integral<T>::value,
                  "No implicit conversion to non-integer type");
    return static_cast<T>(algorithm_);
  }

 private:
  grpc_compression_algorithm algorithm_;
};

class CompressionLevel {
 public:
  enum Level { NONE = 0, LOW = 1, MED = 2, HIGH = 3, COUNT };

  CompressionLevel() : level_(GRPC_COMPRESS_LEVEL_NONE) {}

  CompressionLevel(Level level)
      : level_(static_cast<grpc_compression_level>(level)) {
    static_assert(NONE == static_cast<Level>(GRPC_COMPRESS_LEVEL_NONE),
                  "gRPC Core/C++ compression level value mismatch");
    static_assert(LOW == static_cast<Level>(GRPC_COMPRESS_LEVEL_LOW),
                  "gRPC Core/C++ compression level value mismatch");
    static_assert(MED == static_cast<Level>(GRPC_COMPRESS_LEVEL_MED),
                  "gRPC Core/C++ compression level value mismatch");
    static_assert(HIGH == static_cast<Level>(GRPC_COMPRESS_LEVEL_HIGH),
                  "gRPC Core/C++ compression level value mismatch");
    static_assert(COUNT == static_cast<Level>(GRPC_COMPRESS_LEVEL_COUNT),
                  "gRPC Core/C++ compression level value mismatch");
  }
  CompressionLevel(grpc_compression_level level) : level_(level) {}

  CompressionLevel(const CompressionLevel& level) : level_(level.level_) {}

  CompressionLevel(CompressionLevel&& level) : level_(level.level_) {}

  CompressionLevel& operator=(const CompressionLevel& level) {
    level_ = level.level_;
    return *this;
  }

  CompressionLevel& operator=(CompressionLevel&& level) {
    level_ = level.level_;
    return *this;
  }

  /// Provide a conversion to Algorithm to support comparison against a
  /// specific compression algorithm
  operator Level() const { return static_cast<Level>(level_); }

  /// This conversion operator is needed to maintain API compatibility
  /// with older release versions of the C++ API that returned
  /// grpc_compression_level from ServerContext rather than
  /// grpc::CompressionLevel
  operator grpc_compression_level() const { return level_; }

  /// Having 2 ambiguous enum conversions is ambiguous for gtest which needs
  /// to print it as an integral type, so provide a general integral conversion
  template <class T>
  operator T() const {
    static_assert(std::is_integral<T>::value,
                  "No implicit conversion to non-integer type");
    return static_cast<T>(level_);
  }

 private:
  grpc_compression_level level_;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_COMPRESSION_H
