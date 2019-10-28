#include <stdbool.h>
#include <stdint.h>

#include "src/cpp/ext/filters/census/context.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  uint64_t server_elapsed_time;
  grpc::ServerStatsDeserialize(reinterpret_cast<const char*>(data), size,
                               &server_elapsed_time);
  return 0;
}
