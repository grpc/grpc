//
//
// Copyright 2019 gRPC authors.
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

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <stdint.h>

#include <cstdint>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "src/core/lib/compression/message_compress.h"

using fuzztest::Arbitrary;
using fuzztest::ElementOf;
using fuzztest::VectorOf;

void CheckDecompresses(grpc_compression_algorithm compression_algorithm,
                       std::vector<uint8_t> buffer) {
  grpc_init();
  grpc_slice_buffer input_buffer;
  grpc_slice_buffer_init(&input_buffer);
  grpc_slice_buffer_add(
      &input_buffer,
      grpc_slice_from_copied_buffer(
          reinterpret_cast<const char*>(buffer.data()), buffer.size()));
  grpc_slice_buffer output_buffer;
  grpc_slice_buffer_init(&output_buffer);

  grpc_msg_decompress(compression_algorithm, &input_buffer, &output_buffer);

  grpc_slice_buffer_destroy(&input_buffer);
  grpc_slice_buffer_destroy(&output_buffer);
  grpc_shutdown();
}
FUZZ_TEST(MyTestSuite, CheckDecompresses)
    .WithDomains(ElementOf({GRPC_COMPRESS_NONE, GRPC_COMPRESS_DEFLATE,
                            GRPC_COMPRESS_GZIP}),
                 VectorOf(Arbitrary<uint8_t>()).WithMinSize(1));
