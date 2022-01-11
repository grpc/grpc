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

#include <grpc/support/port_platform.h>

#include "src/core/lib/compression/compression_internal.h"

#include <stdlib.h>
#include <string.h>

#include <cstdint>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include <grpc/compression.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

const char* CompressionAlgorithmAsString(grpc_compression_algorithm algorithm) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      return "identity";
    case GRPC_COMPRESS_DEFLATE:
      return "deflate";
    case GRPC_COMPRESS_GZIP:
      return "gzip";
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
    default:
      return nullptr;
  }
}

absl::optional<grpc_compression_algorithm> ParseCompressionAlgorithm(
    absl::string_view algorithm) {
  if (algorithm == "identity") {
    return GRPC_COMPRESS_NONE;
  } else if (algorithm == "deflate") {
    return GRPC_COMPRESS_DEFLATE;
  } else if (algorithm == "gzip") {
    return GRPC_COMPRESS_GZIP;
  } else {
    return absl::nullopt;
  }
}

grpc_compression_algorithm
CompressionAlgorithmSet::CompressionAlgorithmForLevel(
    grpc_compression_level level) const {
  GRPC_API_TRACE("grpc_message_compression_algorithm_for_level(level=%d)", 1,
                 ((int)level));
  if (level > GRPC_COMPRESS_LEVEL_HIGH) {
    gpr_log(GPR_ERROR, "Unknown message compression level %d.",
            static_cast<int>(level));
    abort();
  }

  if (level == GRPC_COMPRESS_LEVEL_NONE) {
    return GRPC_COMPRESS_NONE;
  }

  GPR_ASSERT(level > 0);

  /* Establish a "ranking" or compression algorithms in increasing order of
   * compression.
   * This is simplistic and we will probably want to introduce other dimensions
   * in the future (cpu/memory cost, etc). */
  absl::InlinedVector<grpc_compression_algorithm,
                      GRPC_COMPRESS_ALGORITHMS_COUNT>
      algos;
  for (auto algo : {GRPC_COMPRESS_GZIP, GRPC_COMPRESS_DEFLATE}) {
    if (set_.is_set(algo)) {
      algos.push_back(algo);
    }
  }

  if (algos.empty()) {
    return GRPC_COMPRESS_NONE;
  }

  switch (level) {
    case GRPC_COMPRESS_LEVEL_NONE:
      abort(); /* should have been handled already */
    case GRPC_COMPRESS_LEVEL_LOW:
      return algos[0];
    case GRPC_COMPRESS_LEVEL_MED:
      return algos[algos.size() / 2];
    case GRPC_COMPRESS_LEVEL_HIGH:
      return algos.back();
    default:
      abort();
  };
}

CompressionAlgorithmSet CompressionAlgorithmSet::FromUint32(uint32_t value) {
  CompressionAlgorithmSet set;
  for (size_t i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (value & (1u << i)) {
      set.set_.set(i);
    }
  }
  return set;
}

CompressionAlgorithmSet CompressionAlgorithmSet::FromChannelArgs(
    const grpc_channel_args* args) {
  CompressionAlgorithmSet set;
  static const uint32_t kEverything =
      (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
  if (args != nullptr) {
    set = CompressionAlgorithmSet::FromUint32(grpc_channel_args_find_integer(
        args, GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
        grpc_integer_options{kEverything, 0, kEverything}));
    set.Set(GRPC_COMPRESS_NONE);
  } else {
    set = CompressionAlgorithmSet::FromUint32(kEverything);
  }
  return set;
}

CompressionAlgorithmSet::CompressionAlgorithmSet() = default;

CompressionAlgorithmSet::CompressionAlgorithmSet(
    std::initializer_list<grpc_compression_algorithm> algorithms) {
  for (auto algorithm : algorithms) {
    Set(algorithm);
  }
}

bool CompressionAlgorithmSet::IsSet(
    grpc_compression_algorithm algorithm) const {
  size_t i = static_cast<size_t>(algorithm);
  if (i < GRPC_COMPRESS_ALGORITHMS_COUNT) {
    return set_.is_set(i);
  } else {
    return false;
  }
}

void CompressionAlgorithmSet::Set(grpc_compression_algorithm algorithm) {
  size_t i = static_cast<size_t>(algorithm);
  if (i < GRPC_COMPRESS_ALGORITHMS_COUNT) {
    set_.set(i);
  }
}

std::string CompressionAlgorithmSet::ToString() const {
  absl::InlinedVector<const char*, GRPC_COMPRESS_ALGORITHMS_COUNT> segments;
  for (size_t i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (set_.is_set(i)) {
      segments.push_back(CompressionAlgorithmAsString(
          static_cast<grpc_compression_algorithm>(i)));
    }
  }
  return absl::StrJoin(segments, ", ");
}

Slice CompressionAlgorithmSet::ToSlice() const {
  return Slice::FromCopiedString(ToString());
}

CompressionAlgorithmSet CompressionAlgorithmSet::FromString(
    absl::string_view str) {
  CompressionAlgorithmSet set{GRPC_COMPRESS_NONE};
  for (auto algorithm : absl::StrSplit(str, ',')) {
    auto parsed =
        ParseCompressionAlgorithm(absl::StripAsciiWhitespace(algorithm));
    if (parsed.has_value()) {
      set.Set(*parsed);
    }
  }
  return set;
}

uint32_t CompressionAlgorithmSet::ToLegacyBitmask() const {
  uint32_t x = 0;
  for (size_t i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (set_.is_set(i)) {
      x |= (1u << i);
    }
  }
  return x;
}

absl::optional<grpc_compression_algorithm>
DefaultCompressionAlgorithmFromChannelArgs(const grpc_channel_args* args) {
  if (args == nullptr) return absl::nullopt;
  for (size_t i = 0; i < args->num_args; i++) {
    if (strcmp(args->args[i].key, GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM) ==
        0) {
      if (args->args[i].type == GRPC_ARG_INTEGER) {
        return static_cast<grpc_compression_algorithm>(
            args->args[i].value.integer);
      } else if (args->args[i].type == GRPC_ARG_STRING) {
        return ParseCompressionAlgorithm(args->args[i].value.string);
      }
    }
  }
  return absl::nullopt;
}

}  // namespace grpc_core
