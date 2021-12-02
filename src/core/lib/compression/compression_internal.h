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

#ifndef GRPC_CORE_LIB_COMPRESSION_COMPRESSION_INTERNAL_H
#define GRPC_CORE_LIB_COMPRESSION_COMPRESSION_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <initializer_list>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/compression.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

absl::optional<grpc_compression_algorithm> ParseCompressionAlgorithm(
    absl::string_view algorithm);
const char* CompressionAlgorithmAsString(grpc_compression_algorithm algorithm);
absl::optional<grpc_compression_algorithm> DefaultCompressionAlgorithmFromChannelArgs(const grpc_channel_args* args);

class CompressionAlgorithmSet {
 public:
  static CompressionAlgorithmSet FromUint32(uint32_t value);
  static CompressionAlgorithmSet FromChannelArgs(const grpc_channel_args* args);
  static CompressionAlgorithmSet FromString(absl::string_view str);
  CompressionAlgorithmSet();
  CompressionAlgorithmSet(
      std::initializer_list<grpc_compression_algorithm> algorithms);

  grpc_compression_algorithm CompressionAlgorithmForLevel(
      grpc_compression_level level) const;
  bool IsSet(grpc_compression_algorithm algorithm) const;
  void Set(grpc_compression_algorithm algorithm);

  std::string ToString() const;
  Slice ToSlice() const;

  uint32_t ToLegacyBitmask() const;

 private:
  BitSet<GRPC_COMPRESS_ALGORITHMS_COUNT> set_;
};

}  // namespace grpc_core


#endif /* GRPC_CORE_LIB_COMPRESSION_COMPRESSION_INTERNAL_H */
