#include <grpc/grpc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "src/core/lib/compression/stream_compression.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/util/memory_counters.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_core::testing::LeakDetector leak_detector(true);
  grpc_init();
  grpc_test_only_control_plane_credentials_force_init();
  auto* context = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
  grpc_slice_buffer input_buffer;
  grpc_slice_buffer_init(&input_buffer);
  grpc_slice_buffer_add(
      &input_buffer,
      grpc_slice_from_copied_buffer(reinterpret_cast<const char*>(data), size));
  grpc_slice_buffer output_buffer;
  grpc_slice_buffer_init(&output_buffer);
  bool end_of_context;

  grpc_stream_decompress(context, &input_buffer, &output_buffer, nullptr,
                         (~(size_t)0), &end_of_context);

  grpc_stream_compression_context_destroy(context);
  grpc_slice_buffer_destroy(&input_buffer);
  grpc_slice_buffer_destroy(&output_buffer);
  grpc_test_only_control_plane_credentials_destroy();
  grpc_shutdown_blocking();
  return 0;
}
