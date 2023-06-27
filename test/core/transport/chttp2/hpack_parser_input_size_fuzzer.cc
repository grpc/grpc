// Copyright 2023 gRPC authors.
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

// For all inputs, ensure parsing one byte at a time produces the same result as
// parsing the entire input at once.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "test/core/util/slice_splitter.h"

bool squelch = true;
bool leak_check = true;

namespace grpc_core {
namespace {

class TestEncoder {
 public:
  std::string result() { return out_; }

  void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
    out_.append(
        absl::StrCat(key.as_string_view(), ": ", value.as_string_view(), "\n"));
  }

  template <typename T, typename V>
  void Encode(T, const V& v) {
    out_.append(absl::StrCat(T::key(), ": ", T::DisplayValue(v), "\n"));
  }

 private:
  std::string out_;
};

bool IsStreamError(const absl::Status& status) {
  intptr_t stream_id;
  return grpc_error_get_int(status, grpc_core::StatusIntProperty::kStreamId,
                            &stream_id);
}

absl::StatusOr<std::string> TestVector(grpc_slice_split_mode mode,
                                       Slice input) {
  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  auto arena = grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_core::ExecCtx exec_ctx;
  grpc_slice* slices;
  size_t nslices;
  size_t i;

  grpc_metadata_batch b(arena.get());

  HPackParser parser;
  parser.BeginFrame(&b, 1024, 1024, grpc_core::HPackParser::Boundary::None,
                    grpc_core::HPackParser::Priority::None,
                    grpc_core::HPackParser::LogInfo{
                        1, grpc_core::HPackParser::LogInfo::kHeaders, false});

  grpc_split_slices(mode, const_cast<grpc_slice*>(&input.c_slice()), 1, &slices,
                    &nslices);
  auto cleanup_slices = absl::MakeCleanup([slices, nslices] {
    for (size_t i = 0; i < nslices; i++) {
      grpc_slice_unref(slices[i]);
    }
    gpr_free(slices);
  });

  absl::Status found_err;
  for (i = 0; i < nslices; i++) {
    grpc_core::ExecCtx exec_ctx;
    auto err = parser.Parse(slices[i], i == nslices - 1);
    if (!err.ok()) {
      if (!IsStreamError(err)) return err;
      if (found_err.ok()) found_err = err;
    }
  }
  if (!found_err.ok()) return found_err;

  TestEncoder encoder;
  b.Encode(&encoder);
  return encoder.result();
}

std::string Stringify(absl::StatusOr<std::string> result) {
  if (result.ok()) {
    return absl::StrCat("OK\n", result.value());
  } else {
    intptr_t stream_id;
    bool has_stream = grpc_error_get_int(
        result.status(), grpc_core::StatusIntProperty::kStreamId, &stream_id);
    return absl::StrCat(
        has_stream ? "STREAM" : "CONNECTION", " ERROR: ",
        result.status().ToString(absl::StatusToStringMode::kWithNoExtraData));
  }
}

}  // namespace
}  // namespace grpc_core

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  gpr_now_impl = [](gpr_clock_type clock_type) {
    return gpr_timespec{10, 0, clock_type};
  };
  auto slice = grpc_core::Slice::FromCopiedBuffer(data, size);
  auto full = grpc_core::Stringify(
      grpc_core::TestVector(GRPC_SLICE_SPLIT_IDENTITY, slice.Ref()));
  auto one_byte = grpc_core::Stringify(
      grpc_core::TestVector(GRPC_SLICE_SPLIT_ONE_BYTE, slice.Ref()));
  if (full != one_byte) {
    fprintf(stderr, "MISMATCHED RESULTS\nFULL SLICE: %s\nONE BYTE: %s\n",
            full.c_str(), one_byte.c_str());
    abort();
  }
  return 0;
}
