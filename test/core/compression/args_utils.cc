// Copyright 2021 gRPC authors.
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

#include "test/core/compression/args_utils.h"

#include <string.h>

#include "absl/types/optional.h"

#include <grpc/compression.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/gpr/useful.h"

namespace grpc_core {
ChannelArgs SetCompressionAlgorithmState(const ChannelArgs& args,
                                         grpc_compression_algorithm algorithm,
                                         bool enabled) {
  auto state = args.GetInt(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET)
                   .value_or(0);
  SetBit(&state, GRPC_COMPRESS_NONE);
  if (enabled) {
    SetBit(&state, algorithm);
  } else {
    ClearBit(&state, algorithm);
  }
  return args.Set(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET, state);
}
}  // namespace grpc_core
