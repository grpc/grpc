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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_TRANSACTION_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_TRANSACTION_H

#include <grpc/impl/codegen/port_platform.h>

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_utils.h"

namespace grpc_binder {

ABSL_CONST_INIT extern const int kFlagPrefix;
ABSL_CONST_INIT extern const int kFlagMessageData;
ABSL_CONST_INIT extern const int kFlagSuffix;
ABSL_CONST_INIT extern const int kFlagOutOfBandClose;
ABSL_CONST_INIT extern const int kFlagExpectSingleMessage;
ABSL_CONST_INIT extern const int kFlagStatusDescription;
ABSL_CONST_INIT extern const int kFlagMessageDataIsParcelable;
ABSL_CONST_INIT extern const int kFlagMessageDataIsPartial;

struct KeyValuePair {
  grpc_slice key;
  grpc_slice value;

  KeyValuePair() : key(grpc_empty_slice()), value(grpc_empty_slice()) {}

  ~KeyValuePair() {
    grpc_slice_unref_internal(key);
    grpc_slice_unref_internal(value);
  }

  // TODO(waynetu): Remove this constructor when we can directly read grpc_slice
  // from binder.
  KeyValuePair(std::string k, std::string v)
      : key(grpc_slice_from_cpp_string(std::move(k))),
        value(grpc_slice_from_cpp_string(std::move(v))) {}
  KeyValuePair(grpc_slice k, grpc_slice v) : key(k), value(v) {}
  KeyValuePair(const KeyValuePair& kv)
      : key(grpc_slice_ref_internal(kv.key)),
        value(grpc_slice_ref_internal(kv.value)) {}

  absl::string_view ViewKey() const {
    return grpc_core::StringViewFromSlice(key);
  }
  absl::string_view ViewValue() const {
    return grpc_core::StringViewFromSlice(value);
  }

  bool operator==(const KeyValuePair& other) const {
    return grpc_slice_eq(key, other.key) && grpc_slice_eq(value, other.value);
  }
};

using Metadata = std::vector<KeyValuePair>;

// Copying grpc_slice_buffer correctly is painful due to inlining. SliceBuffer
// serves as a modern not-so-optimal representation of a buffer of slices.
// The SliceBuffer *does not* own the slices it contains. Instead, others should
// be responsible for un-refing the slice.
using SliceBuffer = std::vector<grpc_slice>;

class Transaction {
 public:
  Transaction(int tx_code, bool is_client)
      : tx_code_(tx_code),
        is_client_(is_client),
        method_ref_(grpc_empty_slice()),
        status_desc_(grpc_empty_slice()) {}

  ~Transaction() {
    grpc_slice_unref_internal(method_ref_);
    grpc_slice_unref_internal(status_desc_);
  }

  void SetPrefix(Metadata prefix_metadata) {
    prefix_metadata_ = std::move(prefix_metadata);
    GPR_ASSERT((flags_ & kFlagPrefix) == 0);
    flags_ |= kFlagPrefix;
  }
  void SetMethodRef(grpc_slice method_ref) {
    GPR_ASSERT(is_client_);
    method_ref_ = method_ref;
  }
  void SetMessageData() { flags_ |= kFlagMessageData; }
  void PushMessageData(grpc_slice message_slice) {
    // We should call SetMessageData() before start pushing message data.
    GPR_ASSERT((flags_ & kFlagMessageData) != 0);
    message_data_.push_back(message_slice);
  }
  void SetSuffix(Metadata suffix_metadata) {
    if (is_client_) GPR_ASSERT(suffix_metadata.empty());
    suffix_metadata_ = std::move(suffix_metadata);
    GPR_ASSERT((flags_ & kFlagSuffix) == 0);
    flags_ |= kFlagSuffix;
  }
  void SetStatusDescription(grpc_slice status_desc) {
    GPR_ASSERT(!is_client_);
    GPR_ASSERT((flags_ & kFlagStatusDescription) == 0);
    status_desc_ = status_desc;
  }
  void SetStatus(int status) {
    GPR_ASSERT(!is_client_);
    GPR_ASSERT((flags_ >> 16) == 0);
    GPR_ASSERT(status < (1 << 16));
    flags_ |= (status << 16);
  }

  bool IsClient() const { return is_client_; }
  bool IsServer() const { return !is_client_; }
  int GetTxCode() const { return tx_code_; }
  int GetFlags() const { return flags_; }

  absl::string_view GetMethodRef() const {
    return grpc_core::StringViewFromSlice(method_ref_);
  }
  const SliceBuffer& GetMessageData() const { return message_data_; }
  absl::string_view GetStatusDesc() const {
    return grpc_core::StringViewFromSlice(status_desc_);
  }
  const Metadata& GetPrefixMetadata() const { return prefix_metadata_; }
  const Metadata& GetSuffixMetadata() const { return suffix_metadata_; }

 private:
  int tx_code_;
  bool is_client_;
  Metadata prefix_metadata_;
  Metadata suffix_metadata_;
  grpc_slice method_ref_;
  SliceBuffer message_data_;
  grpc_slice status_desc_;

  int flags_ = 0;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_TRANSACTION_H
