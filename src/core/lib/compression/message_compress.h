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

#ifndef GRPC_SRC_CORE_LIB_COMPRESSION_MESSAGE_COMPRESS_H
#define GRPC_SRC_CORE_LIB_COMPRESSION_MESSAGE_COMPRESS_H

#include <grpc/impl/compression_types.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include <cstdint>
#include <optional>

// compress 'input' to 'output' using 'algorithm'.
// On success, appends compressed slices to output and returns 1.
// On failure, appends uncompressed slices to output and returns 0.
int grpc_msg_compress(grpc_compression_algorithm algorithm,
                      grpc_slice_buffer* input, grpc_slice_buffer* output);

// decompress 'input' to 'output' using 'algorithm'.
// On success, appends slices to output and returns 1.
// On failure, output is unchanged, and returns 0.
int grpc_msg_decompress(grpc_compression_algorithm algorithm,
                        grpc_slice_buffer* input, grpc_slice_buffer* output);

namespace grpc_core {

enum class MessageDecompressionResult { kOk, kError, kTooLarge };

// Decompresses input while allowing at most max_output_size bytes of output.
// On failure, output is unchanged.
MessageDecompressionResult DecompressMessageWithLimit(
    grpc_compression_algorithm algorithm, grpc_slice_buffer* input,
    grpc_slice_buffer* output, std::optional<uint32_t> max_output_size);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_COMPRESSION_MESSAGE_COMPRESS_H
