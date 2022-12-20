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

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

namespace grpc_binder {

ABSL_CONST_INIT extern const int kFlagPrefix;
ABSL_CONST_INIT extern const int kFlagMessageData;
ABSL_CONST_INIT extern const int kFlagSuffix;
ABSL_CONST_INIT extern const int kFlagOutOfBandClose;
ABSL_CONST_INIT extern const int kFlagExpectSingleMessage;
ABSL_CONST_INIT extern const int kFlagStatusDescription;
ABSL_CONST_INIT extern const int kFlagMessageDataIsParcelable;
ABSL_CONST_INIT extern const int kFlagMessageDataIsPartial;

using Metadata = std::vector<std::pair<std::string, std::string>>;

class Transaction {
 public:
  Transaction(int tx_code, bool is_client)
      : tx_code_(tx_code), is_client_(is_client) {}
  // TODO(mingcl): Consider using string_view
  void SetPrefix(Metadata prefix_metadata) {
    prefix_metadata_ = prefix_metadata;
    GPR_ASSERT((flags_ & kFlagPrefix) == 0);
    flags_ |= kFlagPrefix;
  }
  void SetMethodRef(std::string method_ref) {
    GPR_ASSERT(is_client_);
    method_ref_ = method_ref;
  }
  void SetData(std::string message_data) {
    message_data_ = message_data;
    GPR_ASSERT((flags_ & kFlagMessageData) == 0);
    flags_ |= kFlagMessageData;
  }
  void SetSuffix(Metadata suffix_metadata) {
    if (is_client_) GPR_ASSERT(suffix_metadata.empty());
    suffix_metadata_ = suffix_metadata;
    GPR_ASSERT((flags_ & kFlagSuffix) == 0);
    flags_ |= kFlagSuffix;
  }
  void SetStatusDescription(std::string status_desc) {
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

  absl::string_view GetMethodRef() const { return method_ref_; }
  const Metadata& GetPrefixMetadata() const { return prefix_metadata_; }
  const Metadata& GetSuffixMetadata() const { return suffix_metadata_; }
  absl::string_view GetMessageData() const { return message_data_; }
  absl::string_view GetStatusDesc() const { return status_desc_; }

  Transaction(const Transaction&) = delete;
  void operator=(const Transaction&) = delete;

 private:
  int tx_code_;
  bool is_client_;
  Metadata prefix_metadata_;
  Metadata suffix_metadata_;
  std::string method_ref_;
  std::string message_data_;
  std::string status_desc_;

  int flags_ = 0;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_TRANSACTION_H
