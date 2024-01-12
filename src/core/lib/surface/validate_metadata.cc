//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/lib/surface/validate_metadata.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

namespace {
class LegalHeaderKeyBits : public BitSet<256> {
 public:
  constexpr LegalHeaderKeyBits() {
    for (int i = 'a'; i <= 'z'; i++) set(i);
    for (int i = '0'; i <= '9'; i++) set(i);
    set('-');
    set('_');
    set('.');
  }
};
constexpr LegalHeaderKeyBits g_legal_header_key_bits;

ValidateMetadataResult ConformsTo(absl::string_view x,
                                  const BitSet<256>& legal_bits,
                                  ValidateMetadataResult error) {
  for (uint8_t c : x) {
    if (!legal_bits.is_set(c)) {
      return error;
    }
  }
  return ValidateMetadataResult::kOk;
}

absl::Status UpgradeToStatus(ValidateMetadataResult result) {
  if (result == ValidateMetadataResult::kOk) return absl::OkStatus();
  return absl::InternalError(ValidateMetadataResultToString(result));
}

}  // namespace

ValidateMetadataResult ValidateHeaderKeyIsLegal(absl::string_view key) {
  if (key.empty()) {
    return ValidateMetadataResult::kCannotBeZeroLength;
  }
  if (key.size() > UINT32_MAX) {
    return ValidateMetadataResult::kTooLong;
  }
  return ConformsTo(key, g_legal_header_key_bits,
                    ValidateMetadataResult::kIllegalHeaderKey);
}

const char* ValidateMetadataResultToString(ValidateMetadataResult result) {
  switch (result) {
    case ValidateMetadataResult::kOk:
      return "Ok";
    case ValidateMetadataResult::kCannotBeZeroLength:
      return "Metadata keys cannot be zero length";
    case ValidateMetadataResult::kTooLong:
      return "Metadata keys cannot be larger than UINT32_MAX";
    case ValidateMetadataResult::kIllegalHeaderKey:
      return "Illegal header key";
    case ValidateMetadataResult::kIllegalHeaderValue:
      return "Illegal header value";
  }
  GPR_UNREACHABLE_CODE(return "Unknown");
}

}  // namespace grpc_core

static int error2int(grpc_error_handle error) {
  int r = (error.ok());
  return r;
}

grpc_error_handle grpc_validate_header_key_is_legal(const grpc_slice& slice) {
  return grpc_core::UpgradeToStatus(grpc_core::ValidateHeaderKeyIsLegal(
      grpc_core::StringViewFromSlice(slice)));
}

int grpc_header_key_is_legal(grpc_slice slice) {
  return error2int(grpc_validate_header_key_is_legal(slice));
}

namespace {
class LegalHeaderNonBinValueBits : public grpc_core::BitSet<256> {
 public:
  constexpr LegalHeaderNonBinValueBits() {
    for (int i = 32; i <= 126; i++) {
      set(i);
    }
  }
};
constexpr LegalHeaderNonBinValueBits g_legal_header_non_bin_value_bits;
}  // namespace

grpc_error_handle grpc_validate_header_nonbin_value_is_legal(
    const grpc_slice& slice) {
  return grpc_core::UpgradeToStatus(grpc_core::ConformsTo(
      grpc_core::StringViewFromSlice(slice), g_legal_header_non_bin_value_bits,
      grpc_core::ValidateMetadataResult::kIllegalHeaderValue));
}

int grpc_header_nonbin_value_is_legal(grpc_slice slice) {
  return error2int(grpc_validate_header_nonbin_value_is_legal(slice));
}

int grpc_is_binary_header_internal(const grpc_slice& slice) {
  return grpc_key_is_binary_header(GRPC_SLICE_START_PTR(slice),
                                   GRPC_SLICE_LENGTH(slice));
}

int grpc_is_binary_header(grpc_slice slice) {
  return grpc_is_binary_header_internal(slice);
}
