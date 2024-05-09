//
// Copyright 2017 gRPC authors.
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
// Automatically generated by tools/codegen/core/gen_settings_ids.py
//

#include "src/core/ext/transport/chttp2/transport/http2_settings.h"

#include "absl/strings/str_cat.h"

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/transport/http2_errors.h"

namespace grpc_core {

void Http2Settings::Diff(
    bool is_first_send, const Http2Settings& old,
    absl::FunctionRef<void(uint16_t key, uint32_t value)> cb) const {
  if (header_table_size_ != old.header_table_size_) {
    cb(kHeaderTableSizeWireId, header_table_size_);
  }
  if (enable_push_ != old.enable_push_) {
    cb(kEnablePushWireId, enable_push_);
  }
  if (max_concurrent_streams_ != old.max_concurrent_streams_) {
    cb(kMaxConcurrentStreamsWireId, max_concurrent_streams_);
  }
  if (is_first_send || initial_window_size_ != old.initial_window_size_) {
    cb(kInitialWindowSizeWireId, initial_window_size_);
  }
  if (max_frame_size_ != old.max_frame_size_) {
    cb(kMaxFrameSizeWireId, max_frame_size_);
  }
  if (max_header_list_size_ != old.max_header_list_size_) {
    cb(kMaxHeaderListSizeWireId, max_header_list_size_);
  }
  if (allow_true_binary_metadata_ != old.allow_true_binary_metadata_) {
    cb(kGrpcAllowTrueBinaryMetadataWireId, allow_true_binary_metadata_);
  }
  if (preferred_receive_crypto_message_size_ !=
      old.preferred_receive_crypto_message_size_) {
    cb(kGrpcPreferredReceiveCryptoFrameSizeWireId,
       preferred_receive_crypto_message_size_);
  }
}

std::string Http2Settings::WireIdToName(uint16_t wire_id) {
  switch (wire_id) {
    case kHeaderTableSizeWireId:
      return std::string(header_table_size_name());
    case kEnablePushWireId:
      return std::string(enable_push_name());
    case kMaxConcurrentStreamsWireId:
      return std::string(max_concurrent_streams_name());
    case kInitialWindowSizeWireId:
      return std::string(initial_window_size_name());
    case kMaxFrameSizeWireId:
      return std::string(max_frame_size_name());
    case kMaxHeaderListSizeWireId:
      return std::string(max_header_list_size_name());
    case kGrpcAllowTrueBinaryMetadataWireId:
      return std::string(allow_true_binary_metadata_name());
    case kGrpcPreferredReceiveCryptoFrameSizeWireId:
      return std::string(preferred_receive_crypto_message_size_name());
    default:
      return absl::StrCat("UNKNOWN (", wire_id, ")");
  }
}

grpc_http2_error_code Http2Settings::Apply(uint16_t key, uint32_t value) {
  switch (key) {
    case kHeaderTableSizeWireId:
      header_table_size_ = value;
      break;
    case kEnablePushWireId:
      if (value > 1) return GRPC_HTTP2_PROTOCOL_ERROR;
      enable_push_ = value != 0;
      break;
    case kMaxConcurrentStreamsWireId:
      max_concurrent_streams_ = value;
      break;
    case kInitialWindowSizeWireId:
      if (value > max_initial_window_size()) {
        return GRPC_HTTP2_FLOW_CONTROL_ERROR;
      }
      initial_window_size_ = value;
      break;
    case kMaxFrameSizeWireId:
      if (value < min_max_frame_size() || value > max_max_frame_size()) {
        return GRPC_HTTP2_PROTOCOL_ERROR;
      }
      max_frame_size_ = value;
      break;
    case kMaxHeaderListSizeWireId:
      max_header_list_size_ = std::min(value, 16777216u);
      break;
    case kGrpcAllowTrueBinaryMetadataWireId:
      if (value > 1) return GRPC_HTTP2_PROTOCOL_ERROR;
      allow_true_binary_metadata_ = value != 0;
      break;
    case kGrpcPreferredReceiveCryptoFrameSizeWireId:
      preferred_receive_crypto_message_size_ =
          Clamp(value, min_preferred_receive_crypto_message_size(),
                max_preferred_receive_crypto_message_size());
      break;
  }
  return GRPC_HTTP2_NO_ERROR;
}

absl::optional<Http2SettingsFrame> Http2SettingsManager::MaybeSendUpdate() {
  switch (update_state_) {
    case UpdateState::kSending:
      return absl::nullopt;
    case UpdateState::kIdle:
      if (local_ == sent_) return absl::nullopt;
      break;
    case UpdateState::kFirst:
      break;
  }
  Http2SettingsFrame frame;
  local_.Diff(update_state_ == UpdateState::kFirst, sent_,
              [&frame](uint16_t key, uint32_t value) {
                frame.settings.emplace_back(key, value);
              });
  sent_ = local_;
  update_state_ = UpdateState::kSending;
  return frame;
}

bool Http2SettingsManager::AckLastSend() {
  if (update_state_ != UpdateState::kSending) return false;
  update_state_ = UpdateState::kIdle;
  acked_ = sent_;
  return true;
}

}  // namespace grpc_core
