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

#include "src/core/call/metadata_info.h"

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <string>

#include "absl/strings/str_cat.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

class MetadataSizesAnnotation::MetadataSizeEncoder {
 public:
  explicit MetadataSizeEncoder(std::string& summary, uint64_t soft_limit,
                               uint64_t hard_limit)
      : summary_(summary) {
    header_ = absl::StrCat("gRPC metadata soft_limit:", soft_limit,
                           ",hard_limit:", hard_limit, ",");
    absl::StrAppend(&summary_, header_);
    entry_length_ = header_.length();
  }

  void Encode(const Slice& key, const Slice& value) {
    AddToSummary(key.as_string_view(), value.size());
  }

  template <typename Key, typename Value>
  void Encode(Key, const Value& value) {
    AddToSummary(Key::key(), EncodedSizeOfKey(Key(), value));
  }

 private:
  void AddToSummary(absl::string_view key,
                    size_t value_length) GPR_ATTRIBUTE_NOINLINE {
    std::string metadata_str = absl::StrCat(
        key, ":", hpack_constants::SizeForEntry(key.size(), value_length), ",");
    if ((entry_length_ + metadata_str.length()) < 200) {
      // Limit each annotation to 200 bytes.
      entry_length_ += metadata_str.length();
      absl::StrAppend(&summary_, metadata_str);
    } else {
      absl::StrAppend(&summary_, ";", header_, metadata_str);
      entry_length_ = header_.length() + metadata_str.length();
    }
  }
  std::string& summary_;
  std::string header_;
  size_t entry_length_ = 0;
};

std::string MetadataSizesAnnotation::ToString() const {
  std::string metadata_annotation;
  MetadataSizeEncoder encoder(metadata_annotation, soft_limit_, hard_limit_);
  metadata_buffer_->Encode(&encoder);
  return metadata_annotation;
}

}  // namespace grpc_core