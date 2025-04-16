//
//
// Copyright 2025 gRPC authors.
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

#include <grpc/grpc.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace http2 {
namespace testing {

static uint64_t fuzz_stats_total_len = 0;
static uint64_t fuzz_stats_num_inputs = 0;

SliceBuffer GetFuzzedPayload(
    const std::variant<std::vector<uint8_t>, uint8_t>& step) {
  const std::vector<uint8_t>* fuzzed_input =
      std::get_if<std::vector<uint8_t>>(&step);
  SliceBuffer fuzzed_payload;
  if (fuzzed_input != nullptr) {
    fuzzed_payload.Append(Slice::FromCopiedBuffer(*fuzzed_input));
  }
  return fuzzed_payload;
}

void AssemblerFuzzer(
    std::vector<std::variant<std::vector<uint8_t>, uint8_t>> steps) {
  // Fuzzing tests for class GrpcMessageAssembler
  // This tests AppendNewDataFrame and ExtractMessage methods.
  GrpcMessageAssembler assembler;
  LOG(INFO) << "AssemblerFuzzer num_steps: " << steps.size();
  size_t count_steps = 0;
  for (const auto& step : steps) {
    ++count_steps;
    if (std::holds_alternative<std::vector<uint8_t>>(step)) {
      SliceBuffer payload = GetFuzzedPayload(step);
      fuzz_stats_total_len += payload.Length();
      ++fuzz_stats_num_inputs;
      LOG(INFO) << "    AssemblerFuzzer Data : { Step:" << count_steps
                << ", Length: " << payload.Length()
                << ", Payload:" << payload.JoinIntoString() << " }";
      // TODO(tjagtap) : [PH2][P4] : AppendNewDataFrame has a DCHECK which does
      // not allow for any more calls of AppendNewDataFrame after
      // AppendNewDataFrame(is_end_stream = true) has been called.
      // To avoid this test DCHECK, we are always passing is_end_stream as
      // false. Consider computing the index of the last index payload in each
      // step and setting is_end_stream to true for the last payload.
      assembler.AppendNewDataFrame(payload, /*is_end_stream=*/false);
      EXPECT_EQ(payload.Length(), 0);
    } else {
      CHECK(std::holds_alternative<uint8_t>(step));
      const uint8_t num_msgs = std::get<uint8_t>(step);
      LOG(INFO) << "    AssemblerFuzzer Extract : { Step:" << count_steps
                << ", Number of extracts: " << static_cast<int>(num_msgs)
                << " }";
      for (uint8_t count_msgs = 0; count_msgs < num_msgs; count_msgs++) {
        absl::StatusOr<MessageHandle> result = assembler.ExtractMessage();
        if (!result.ok()) {
          // The fuzzing input did not have the right amount of bytes.
          // While this would be a bug for real transport code, for a fuzz test
          // getting an error is expected.
          LOG(INFO) << "    AssemblerFuzzer Extract Error: " << result.status();
          break;
        } else if (*result == nullptr) {
          // It is rare to reach this point when running with a fuzzer.
          // We reach here if there is no more data to extract.
          LOG(INFO) << "    AssemblerFuzzer Extract : No more data";
          break;
        } else {
          LOG(INFO) << "    AssemblerFuzzer Extracted "
                    << (*result)->payload()->Length() << " Bytes";
        }
      }
    }
  }
  LOG(INFO) << "    AssemblerFuzzer Stats: Total len: " << fuzz_stats_total_len
            << ", Num inputs: " << fuzz_stats_num_inputs
            << " Average Input Length: "
            << (static_cast<double>(fuzz_stats_num_inputs) /
                fuzz_stats_total_len);
  // TODO(tjagtap) : [PH2][P4] : Currently the average size of the payload is
  // usually around 0.91. Write more tests to test with :
  // 1. Mixed sized payloads with average payload size 1KB.
  // 2. A mix of valid gRPC messages and malformed gRPC messages. In this test
  // all are malformed gRPC messages.
  // 3. Valid and invalid states of is_end_stream.
}

FUZZ_TEST(GrpcMessageAssemblerTest, AssemblerFuzzer);

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
