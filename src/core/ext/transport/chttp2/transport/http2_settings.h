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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_H

#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <cstdint>
#include <optional>

#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/util/useful.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class Http2Settings {
 public:
  enum : uint16_t {
    // These values are as defined in RFC9113
    // https://www.rfc-editor.org/rfc/rfc9113.html#name-defined-settings
    kHeaderTableSizeWireId = 1,
    kEnablePushWireId = 2,
    kMaxConcurrentStreamsWireId = 3,
    kInitialWindowSizeWireId = 4,
    kMaxFrameSizeWireId = 5,
    kMaxHeaderListSizeWireId = 6,
    // gRPC specific settings
    kGrpcAllowTrueBinaryMetadataWireId = 65027,
    kGrpcPreferredReceiveCryptoFrameSizeWireId = 65028,
    kGrpcAllowSecurityFrameWireId = 65029,
  };

  void Diff(bool is_first_send, const Http2Settings& old_setting,
            absl::FunctionRef<void(uint16_t key, uint32_t value)> cb) const;
  GRPC_MUST_USE_RESULT http2::Http2ErrorCode Apply(uint16_t key,
                                                   uint32_t value);
  uint32_t header_table_size() const { return header_table_size_; }
  uint32_t max_concurrent_streams() const { return max_concurrent_streams_; }
  uint32_t initial_window_size() const { return initial_window_size_; }
  uint32_t max_frame_size() const { return max_frame_size_; }
  uint32_t max_header_list_size() const { return max_header_list_size_; }
  uint32_t preferred_receive_crypto_message_size() const {
    return preferred_receive_crypto_message_size_;
  }
  bool enable_push() const { return enable_push_; }
  bool allow_true_binary_metadata() const {
    return allow_true_binary_metadata_;
  }
  bool allow_security_frame() const { return allow_security_frame_; }

  void SetHeaderTableSize(uint32_t x) { header_table_size_ = x; }
  void SetMaxConcurrentStreams(uint32_t x) {
    initial_max_concurrent_streams_ = x;
    max_concurrent_streams_ = x;
  }
  void UpdateMaxConcurrentStreams(uint32_t x) {
    max_concurrent_streams_ = std::min(x, initial_max_concurrent_streams_);
  }
  void SetInitialWindowSize(uint32_t x) {
    initial_window_size_ = std::min(x, max_initial_window_size());
  }
  void SetEnablePush(bool x) { enable_push_ = x; }
  void SetMaxHeaderListSize(uint32_t x) {
    max_header_list_size_ = std::min(x, 16777216u);
  }
  void SetAllowTrueBinaryMetadata(bool x) { allow_true_binary_metadata_ = x; }
  void SetMaxFrameSize(uint32_t x) {
    max_frame_size_ = Clamp(x, min_max_frame_size(), max_max_frame_size());
  }
  void SetPreferredReceiveCryptoMessageSize(uint32_t x) {
    preferred_receive_crypto_message_size_ =
        Clamp(x, min_preferred_receive_crypto_message_size(),
              max_preferred_receive_crypto_message_size());
  }
  void SetAllowSecurityFrame(bool x) { allow_security_frame_ = x; }

  static absl::string_view header_table_size_name() {
    return "HEADER_TABLE_SIZE";
  }
  static absl::string_view max_concurrent_streams_name() {
    return "MAX_CONCURRENT_STREAMS";
  }
  static absl::string_view initial_window_size_name() {
    return "INITIAL_WINDOW_SIZE";
  }
  static absl::string_view max_frame_size_name() { return "MAX_FRAME_SIZE"; }
  static absl::string_view max_header_list_size_name() {
    return "MAX_HEADER_LIST_SIZE";
  }
  static absl::string_view enable_push_name() { return "ENABLE_PUSH"; }
  static absl::string_view allow_true_binary_metadata_name() {
    return "GRPC_ALLOW_TRUE_BINARY_METADATA";
  }
  static absl::string_view preferred_receive_crypto_message_size_name() {
    return "GRPC_PREFERRED_RECEIVE_MESSAGE_SIZE";
  }
  static absl::string_view allow_security_frame_name() {
    return "GRPC_ALLOW_SECURITY_FRAME";
  }

  static uint32_t max_initial_window_size() { return 2147483647u; }
  static uint32_t max_max_frame_size() { return 16777215u; }
  static uint32_t min_max_frame_size() { return 16384u; }
  static uint32_t min_preferred_receive_crypto_message_size() { return 16384u; }
  static uint32_t max_preferred_receive_crypto_message_size() {
    return 2147483647u;
  }

  static std::string WireIdToName(uint16_t wire_id);

  bool operator==(const Http2Settings& rhs) const {
    return header_table_size_ == rhs.header_table_size_ &&
           max_concurrent_streams_ == rhs.max_concurrent_streams_ &&
           initial_window_size_ == rhs.initial_window_size_ &&
           max_frame_size_ == rhs.max_frame_size_ &&
           max_header_list_size_ == rhs.max_header_list_size_ &&
           preferred_receive_crypto_message_size_ ==
               rhs.preferred_receive_crypto_message_size_ &&
           enable_push_ == rhs.enable_push_ &&
           allow_true_binary_metadata_ == rhs.allow_true_binary_metadata_ &&
           allow_security_frame_ == rhs.allow_security_frame_;
  }

  bool operator!=(const Http2Settings& rhs) const { return !operator==(rhs); }

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set(header_table_size_name(), header_table_size())
        .Set(max_concurrent_streams_name(), max_concurrent_streams())
        .Set(initial_window_size_name(), initial_window_size())
        .Set(max_frame_size_name(), max_frame_size())
        .Set(max_header_list_size_name(), max_header_list_size())
        .Set(preferred_receive_crypto_message_size_name(),
             preferred_receive_crypto_message_size())
        .Set(enable_push_name(), enable_push())
        .Set(allow_true_binary_metadata_name(), allow_true_binary_metadata())
        .Set(allow_security_frame_name(), allow_security_frame());
  }

 private:
  // RFC9113 states the default value for SETTINGS_HEADER_TABLE_SIZE
  // Currently this is set only once in the lifetime of a transport.
  // We plan to change that in the future.
  uint32_t header_table_size_ = 4096u;

  // TODO(tjagtap) [PH2][P4] : Get the history of why this default was decided
  // and write it here.
  // CLIENT : Set only once in the lifetime of a client transport. This is set
  // to 0 for client.
  // SERVER : This setting can change for the server. This is usually changed to
  // handle memory pressure.
  uint32_t initial_max_concurrent_streams_ = 4294967295u;
  uint32_t max_concurrent_streams_ = 4294967295u;

  // RFC9113 states the default for SETTINGS_INITIAL_WINDOW_SIZE
  // Both client and servers can change this setting. This is usually changed to
  // handle memory pressure.
  uint32_t initial_window_size_ = 65535u;

  // RFC9113 states the default for SETTINGS_MAX_FRAME_SIZE
  // Both client and servers can change this setting. This is usually changed to
  // handle memory pressure.
  uint32_t max_frame_size_ = 16384u;

  // TODO(tjagtap) [PH2][P4] : Get the history of why this default was decided
  // and write it here.
  // This is an advisory but we currently enforce it.
  // Set only once in the lifetime of a transport currently.
  // When a peer that updates this more than once, that may indicate either an
  // underlying issue or a malicious peer.
  uint32_t max_header_list_size_ = 16777216u;

  // gRPC defined setting
  // Both client and servers can change this setting. This is usually changed to
  // handle memory pressure.
  uint32_t preferred_receive_crypto_message_size_ = 0u;

  // RFC9113 defined default is true. However, for gRPC we always then set it to
  // false via the SetEnablePush function
  // Currently this is set only once in the lifetime of a transport.
  // We have no plans to support this in the future.
  bool enable_push_ = true;

  // gRPC defined setting
  // Unlike most other SETTINGS, this setting is negotiated between the client
  // and the server.
  // Currently this is set only once in the lifetime of a transport.
  // Disconnect if it is received more than once from the peer.
  bool allow_true_binary_metadata_ = false;

  // gRPC defined setting
  // Unlike most other SETTINGS, this setting is negotiated between the client
  // and the server. Both have to set it to true for the system to successfully
  // apply the custom SECURITY frame.
  // Currently this is set only once in the lifetime of a transport.
  // Disconnect if it is received more than once from the peer.
  bool allow_security_frame_ = false;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_H
