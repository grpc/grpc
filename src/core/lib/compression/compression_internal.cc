//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/compression/compression_internal.h"

#include <stdlib.h>

#include <optional>

#include "absl/container/inlined_vector.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "compression_internal.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/ref_counted_string.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

namespace {
const ChannelArgs::IntKey kCompressionEnabledAlgorithmsBitsetKey =
    ChannelArgs::IntKey::Register(
        GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
        ChannelArgs::KeyOptions{});
const ChannelArgs::IntKey kCompressionDefaultAlgorithm =
    ChannelArgs::IntKey::Register(
        GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
        ChannelArgs::KeyOptions().WithParseStringToInt(
            [](absl::string_view algorithm) {
              return ParseCompressionAlgorithm(algorithm);
            }));
}  // namespace

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

namespace {
class CommaSeparatedLists {
 public:
  CommaSeparatedLists() : lists_{}, text_buffer_{} {
    char* text_buffer = text_buffer_;
    auto add_char = [&text_buffer, this](char c) {
      if (text_buffer - text_buffer_ == kTextBufferSize) abort();
      *text_buffer++ = c;
    };
    for (size_t list = 0; list < kNumLists; ++list) {
      char* start = text_buffer;
      for (size_t algorithm = 0; algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
           ++algorithm) {
        if ((list & (1 << algorithm)) == 0) continue;
        if (start != text_buffer) {
          add_char(',');
          add_char(' ');
        }
        const char* name = CompressionAlgorithmAsString(
            static_cast<grpc_compression_algorithm>(algorithm));
        for (const char* p = name; *p != '\0'; ++p) {
          add_char(*p);
        }
      }
      lists_[list] = absl::string_view(start, text_buffer - start);
    }
    if (text_buffer - text_buffer_ != kTextBufferSize) abort();
  }

  absl::string_view operator[](size_t list) const { return lists_[list]; }

 private:
  static constexpr size_t kNumLists = 1 << GRPC_COMPRESS_ALGORITHMS_COUNT;
  // Experimentally determined (tweak things until it runs).
  static constexpr size_t kTextBufferSize = 86;
  absl::string_view lists_[kNumLists];
  char text_buffer_[kTextBufferSize];
};

const CommaSeparatedLists kCommaSeparatedLists;
}  // namespace

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
    Crash(absl::StrFormat("Unknown message compression level %d.",
                          static_cast<int>(level)));
  }

  if (level == GRPC_COMPRESS_LEVEL_NONE) {
    return GRPC_COMPRESS_NONE;
  }

  GPR_ASSERT(level > 0);

  // Establish a "ranking" or compression algorithms in increasing order of
  // compression.
  // This is simplistic and we will probably want to introduce other dimensions
  // in the future (cpu/memory cost, etc).
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
      abort();  // should have been handled already
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
    const ChannelArgs& args) {
  CompressionAlgorithmSet set;
  static const uint32_t kEverything =
      (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
  return CompressionAlgorithmSet::FromUint32(
      args.GetInt(kCompressionEnabledAlgorithmsBitsetKey)
          .value_or(kEverything));
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

absl::string_view CompressionAlgorithmSet::ToString() const {
  return kCommaSeparatedLists[ToLegacyBitmask()];
}

Slice CompressionAlgorithmSet::ToSlice() const {
  return Slice::FromStaticString(ToString());
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
  return set_.ToInt<uint32_t>();
}

absl::optional<grpc_compression_algorithm>
DefaultCompressionAlgorithmFromChannelArgs(const ChannelArgs& args) {
  auto r = args.GetInt(kCompressionDefaultAlgorithm);
  if (!r.has_value()) return absl::nullopt;
  return static_cast<grpc_compression_algorithm>(r.value());
}

}  // namespace grpc_core
