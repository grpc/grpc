#include <fuzzer/FuzzedDataProvider.h>
#include <grpc/grpc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/util/memory_counters.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);
  const auto compression_algorithm =
      static_cast<grpc_message_compression_algorithm>(
          data_provider.ConsumeIntegralInRange<int>(
              0, GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT - 1));
  const auto fuzz_data = data_provider.ConsumeRemainingBytes<char>();

  grpc_core::testing::LeakDetector leak_detector(true);
  grpc_init();
  grpc_test_only_control_plane_credentials_force_init();
  grpc_slice_buffer input_buffer;
  grpc_slice_buffer_init(&input_buffer);
  grpc_slice_buffer_add(&input_buffer, grpc_slice_from_copied_buffer(
                                           fuzz_data.data(), fuzz_data.size()));
  grpc_slice_buffer output_buffer;
  grpc_slice_buffer_init(&output_buffer);

  grpc_msg_decompress(compression_algorithm, &input_buffer, &output_buffer);

  grpc_slice_buffer_destroy(&input_buffer);
  grpc_slice_buffer_destroy(&output_buffer);
  grpc_test_only_control_plane_credentials_destroy();
  grpc_shutdown_blocking();
  return 0;
}
