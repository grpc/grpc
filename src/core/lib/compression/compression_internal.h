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

#include <grpc/compression.h>
#include <grpc/slice.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/gprpp/bitset.h"
#include "absl/types/optional.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::optional<grpc_compression_algorithm> ParseCompressionAlgorithm(absl::string_view algorithm);
const char *CompressionAlgorithmAsString(grpc_compression_algorithm algorithm);

class CompressionAlgorithmSet {
 public:
 static CompressionAlgorithmSet FromUint32(uint32_t value);
  static CompressionAlgorithmSet FromChannelArgs(const grpc_channel_args* args);

  grpc_compression_algorithm CompressionAlgorithmForLevel(grpc_compression_level level)const;
  bool IsSet(grpc_compression_algorithm algorithm)const;

 private:
  BitSet<GRPC_COMPRESS_ALGORITHMS_COUNT> set_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_COMPRESSION_COMPRESSION_INTERNAL_H */
