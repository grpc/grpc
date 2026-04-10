//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_METADATA_BATCH_H
#define GRPC_SRC_CORE_CALL_METADATA_BATCH_H

#include <grpc/impl/compression_types.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "src/core/call/custom_metadata.h"
#include "src/core/call/metadata_compression_traits.h"
#include "src/core/call/parsed_metadata.h"
#include "src/core/call/simple_slice_based_metadata.h"
#include "src/core/call/metadata_unknown_map.h"
#include "src/core/call/metadata_debug_string_builder.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/chunked_vector.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/if_list.h"
#include "src/core/util/packed_table.h"
#include "src/core/util/time.h"
#include "src/core/util/type_list.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/function_ref.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

template <typename Key, typename Value>
size_t EncodedSizeOfKey(Key, const Value& value) {
  return Key::Encode(value).size();
}

struct GrpcLbClientStats;

using MetadataParseErrorFn = absl::FunctionRef<void(absl::string_view, const Slice&)>;


struct GrpcTarPit {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Empty;
  using MementoType = Empty;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcTarPit"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "tarpit";
  }
};
struct GrpcCallWasCancelled {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = bool;
  using MementoType = bool;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcCallWasCancelled"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return x ? "true" : "false";
  }
};
struct GrpcStatusFromWire {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = bool;
  using MementoType = bool;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcStatusFromWire"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return x ? "true" : "false";
  }
};
struct GrpcTrailersOnly {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = bool;
  using MementoType = bool;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcTrailersOnly"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return x ? "true" : "false";
  }
};
struct IsTransparentRetry {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = bool;
  using MementoType = bool;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "IsTransparentRetry"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return x ? "true" : "false";
  }
};
struct EndpointLoadMetricsBinMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "endpoint-load-metrics-bin"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcMessageMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-message"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcServerStatsBinMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-server-stats-bin"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcTagsBinMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-tags-bin"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcTraceBinMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-trace-bin"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HostMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "host"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HttpAuthorityMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return ":authority"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HttpPathMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return ":path"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct LbTokenMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "lb-token"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct PeerString {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "PeerString"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct UserAgentMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "user-agent"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct W3CTraceParentMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "traceparent"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct XEnvoyPeerMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "x-envoy-peer"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct XForwardedForMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "x-forwarded-for"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct XForwardedHostMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Slice;
  using MementoType = Slice;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "x-forwarded-host"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.Ref();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcStatusContext {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = true;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = std::string;
  using MementoType = std::string;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcStatusContext"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcRetryPushbackMsMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Duration;
  using MementoType = Duration;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-retry-pushback-ms"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice::FromInt64(x.millis());
  }
  static auto DisplayValue(const ValueType& x) {
return x.millis();
  }
};
struct GrpcTimeoutMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Timestamp;
  using MementoType = Timestamp;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-timeout"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return x.ToString();
  }
};
struct GrpcLbClientStatsMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = GrpcLbClientStats*;
  using MementoType = GrpcLbClientStats*;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-lb-client-stats"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
on_error("not a valid value for grpclb_client_stats", Slice());
    return nullptr;
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
abort();
  }
  static auto DisplayValue(const ValueType& x) {
return "<internal-lb-stats>";
  }
};
struct GrpcRegisteredMethod {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = void*;
  using MementoType = void*;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcRegisteredMethod"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct ContentTypeMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  enum ValueType : uint8_t {
    kApplicationGrpc,
    kEmpty,
    kInvalid,
  };
  using MementoType = ValueType;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "content-type"; }
  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcAcceptEncodingMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = CompressionAlgorithmSet;
  using MementoType = CompressionAlgorithmSet;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-accept-encoding"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
return CompressionAlgorithmSet::FromString(value.as_string_view());
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return x.ToSlice();
  }
  static auto DisplayValue(const ValueType& x) {
return x.ToString();
  }
};
struct GrpcEncodingMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = grpc_compression_algorithm;
  using MementoType = grpc_compression_algorithm;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-encoding"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcInternalEncodingRequest {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = grpc_compression_algorithm;
  using MementoType = grpc_compression_algorithm;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-internal-encoding-request"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcPreviousRpcAttemptsMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = uint32_t;
  using MementoType = uint32_t;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-previous-rpc-attempts"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct GrpcStatusMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = grpc_status_code;
  using MementoType = grpc_status_code;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "grpc-status"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice::FromInt64(x);
  }
  static auto DisplayValue(const ValueType& x) {
switch (x) {
      case GRPC_STATUS_OK:
        return "OK";
      case GRPC_STATUS_CANCELLED:
        return "CANCELLED";
      case GRPC_STATUS_UNKNOWN:
        return "UNKNOWN";
      case GRPC_STATUS_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
      case GRPC_STATUS_DEADLINE_EXCEEDED:
        return "DEADLINE_EXCEEDED";
      case GRPC_STATUS_NOT_FOUND:
        return "NOT_FOUND";
      case GRPC_STATUS_ALREADY_EXISTS:
        return "ALREADY_EXISTS";
      case GRPC_STATUS_PERMISSION_DENIED:
        return "PERMISSION_DENIED";
      case GRPC_STATUS_RESOURCE_EXHAUSTED:
        return "RESOURCE_EXHAUSTED";
      case GRPC_STATUS_FAILED_PRECONDITION:
        return "FAILED_PRECONDITION";
      case GRPC_STATUS_ABORTED:
        return "ABORTED";
      case GRPC_STATUS_OUT_OF_RANGE:
        return "OUT_OF_RANGE";
      case GRPC_STATUS_UNIMPLEMENTED:
        return "UNIMPLEMENTED";
      case GRPC_STATUS_INTERNAL:
        return "INTERNAL";
      case GRPC_STATUS_UNAVAILABLE:
        return "UNAVAILABLE";
      case GRPC_STATUS_DATA_LOSS:
        return "DATA_LOSS";
      case GRPC_STATUS_UNAUTHENTICATED:
        return "UNAUTHENTICATED";
      default:
        return "<UNKNOWN>";
    }
  }
};
struct GrpcStreamNetworkState {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  enum ValueType : uint8_t {
    kNotSentOnWire,
    kNotSeenByServer,
  };
  using MementoType = ValueType;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "GrpcStreamNetworkState"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HttpMethodMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return ":method"; }
  enum class ValueType : uint32_t { kPost, kGet, kPut, kInvalid };
  using MementoType = ValueType;
  static constexpr auto kPost = ValueType::kPost;
  static constexpr auto kGet = ValueType::kGet;
  static constexpr auto kPut = ValueType::kPut;
  static constexpr auto kInvalid = ValueType::kInvalid;
  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error) {
    if (value == "POST") return kPost;
    if (value == "PUT") return kPut;
    if (value == "GET") return kGet;
    on_error("invalid value", Slice::FromCopiedBuffer(value));
    return kInvalid;
  }
  static StaticSlice Encode(ValueType x) {
    switch (x) {
      case kPost: return StaticSlice::FromStaticString("POST");
      case kPut: return StaticSlice::FromStaticString("PUT");
      case kGet: return StaticSlice::FromStaticString("GET");
      default: return StaticSlice::FromStaticString("<<INVALID METHOD>>");
    }
  }
  static const char* DisplayValue(ValueType x) {
    switch (x) {
      case kPost: return "POST";
      case kGet: return "GET";
      case kPut: return "PUT";
      default: return "<discarded-invalid-value>";
    }
  }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
};
struct HttpSchemeMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return ":scheme"; }
  enum class ValueType : uint32_t { kHttp, kHttps, kInvalid };
  using MementoType = ValueType;
  static constexpr auto kHttp = ValueType::kHttp;
  static constexpr auto kHttps = ValueType::kHttps;
  static constexpr auto kInvalid = ValueType::kInvalid;
  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error) {
    if (value == "http") return kHttp;
    if (value == "https") return kHttps;
    on_error("invalid value", Slice::FromCopiedBuffer(value));
    return kInvalid;
  }
  static StaticSlice Encode(ValueType x) {
    switch (x) {
      case kHttp: return StaticSlice::FromStaticString("http");
      case kHttps: return StaticSlice::FromStaticString("https");
      default: abort();
    }
  }
  static const char* DisplayValue(ValueType x) {
    switch (x) {
      case kHttp: return "http";
      case kHttps: return "https";
      default: return "<discarded-invalid-value>";
    }
  }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
return Parse(value.as_string_view(), on_error);
  }
  static ValueType MementoToValue(MementoType x) { return x; }
};
struct HttpStatusMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = uint32_t;
  using MementoType = uint32_t;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return ":status"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct LbCostBinMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = true;
  static constexpr bool kTransferOnTrailersOnly = false;
  struct ValueType { double cost; std::string name; };
  using MementoType = ValueType;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view key() { return "lb-cost-bin"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
if (value.length() < sizeof(double)) {
  on_error("too short", value);
  return {0, ""};
}
MementoType out;
memcpy(&out.cost, value.data(), sizeof(double));
out.name = std::string(reinterpret_cast<const char*>(value.data()) + sizeof(double), value.length() - sizeof(double));
return out;

  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
auto slice = MutableSlice::CreateUninitialized(sizeof(double) + x.name.length());
memcpy(slice.data(), &x.cost, sizeof(double));
memcpy(slice.data() + sizeof(double), x.name.data(), x.name.length());
return Slice(std::move(slice));

  }
  static auto DisplayValue(const ValueType& x) {
return absl::StrCat(x.name, ":", x.cost);

  }
};
struct TeMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  enum ValueType : uint8_t {
    kTrailers,
    kInvalid,
  };
  using MementoType = ValueType;
  using CompressionTraits = KnownValueCompressor<ValueType, kTrailers>;
  static absl::string_view key() { return "te"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
if (value == StaticSlice::FromStaticString("trailers")) return kTrailers; return kInvalid;
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
GRPC_CHECK(x == kTrailers);
    return Slice(StaticSlice::FromStaticString("trailers"));
  }
  static auto DisplayValue(const ValueType& x) {
if (x == kTrailers) return "trailers"; return "invalid";
  }
};
struct WaitForReady {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  struct ValueType { bool value = false; bool explicitly_set = false; };
  using MementoType = ValueType;
  using CompressionTraits = NoCompressionCompressor;
  static absl::string_view DebugKey() { return "WaitForReady"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(const ValueType& x) {
return Slice(StaticSlice::FromStaticString("foo"));
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};

template <typename Which>
absl::enable_if_t<std::is_same<typename Which::ValueType, Slice>::value, const Slice&>
MetadataValueAsSlice(const Slice& slice) { return slice; }

template <typename Which>
absl::enable_if_t<!std::is_same<typename Which::ValueType, Slice>::value, Slice>
MetadataValueAsSlice(typename Which::ValueType value) { return Slice(Which::Encode(value)); }

template <template <typename, typename> class Factory, typename... MetadataTraits>
struct StatefulCompressor {};

struct grpc_metadata_batch {
 private:

  uint8_t flags_[6] = {0};
  Slice EndpointLoadMetricsBinMetadata_;
  Slice GrpcMessageMetadata_;
  Slice GrpcServerStatsBinMetadata_;
  Slice GrpcTagsBinMetadata_;
  Slice GrpcTraceBinMetadata_;
  Slice HostMetadata_;
  Slice HttpAuthorityMetadata_;
  Slice HttpPathMetadata_;
  Slice LbTokenMetadata_;
  Slice PeerString_;
  Slice UserAgentMetadata_;
  Slice W3CTraceParentMetadata_;
  Slice XEnvoyPeerMetadata_;
  Slice XForwardedForMetadata_;
  Slice XForwardedHostMetadata_;
  absl::InlinedVector<std::string, 1> GrpcStatusContext_;
  Duration GrpcRetryPushbackMsMetadata_;
  Timestamp GrpcTimeoutMetadata_;
  GrpcLbClientStats* GrpcLbClientStatsMetadata_;
  void* GrpcRegisteredMethod_;
  ContentTypeMetadata::ValueType ContentTypeMetadata_;
  CompressionAlgorithmSet GrpcAcceptEncodingMetadata_;
  grpc_compression_algorithm GrpcEncodingMetadata_;
  grpc_compression_algorithm GrpcInternalEncodingRequest_;
  uint32_t GrpcPreviousRpcAttemptsMetadata_;
  grpc_status_code GrpcStatusMetadata_;
  GrpcStreamNetworkState::ValueType GrpcStreamNetworkState_;
  HttpMethodMetadata::ValueType HttpMethodMetadata_;
  HttpSchemeMetadata::ValueType HttpSchemeMetadata_;
  uint32_t HttpStatusMetadata_;
  absl::InlinedVector<LbCostBinMetadata::ValueType, 1> LbCostBinMetadata_;
  TeMetadata::ValueType TeMetadata_;
  WaitForReady::ValueType WaitForReady_;
  metadata_detail::UnknownMap unknown_;

 public:
  grpc_metadata_batch() = default;
  ~grpc_metadata_batch() = default;
  grpc_metadata_batch(const grpc_metadata_batch&) = delete;
  grpc_metadata_batch& operator=(const grpc_metadata_batch&) = delete;
  grpc_metadata_batch(grpc_metadata_batch&& other) noexcept {
    flags_[0] = other.flags_[0];
    other.flags_[0] = 0;
    // bool_flags_ optimized out by FlagAllocator unified array
    flags_[1] = other.flags_[1];
    other.flags_[1] = 0;
    // bool_flags_ optimized out by FlagAllocator unified array
    flags_[2] = other.flags_[2];
    other.flags_[2] = 0;
    // bool_flags_ optimized out by FlagAllocator unified array
    flags_[3] = other.flags_[3];
    other.flags_[3] = 0;
    // bool_flags_ optimized out by FlagAllocator unified array
    flags_[4] = other.flags_[4];
    other.flags_[4] = 0;
    // bool_flags_ optimized out by FlagAllocator unified array
    flags_[5] = other.flags_[5];
    other.flags_[5] = 0;
    // bool_flags_ optimized out by FlagAllocator unified array
    if ((flags_[0] & (1 << 5)) != 0) {
      EndpointLoadMetricsBinMetadata_ = std::move(other.EndpointLoadMetricsBinMetadata_);
    }
    if ((flags_[0] & (1 << 6)) != 0) {
      GrpcMessageMetadata_ = std::move(other.GrpcMessageMetadata_);
    }
    if ((flags_[0] & (1 << 7)) != 0) {
      GrpcServerStatsBinMetadata_ = std::move(other.GrpcServerStatsBinMetadata_);
    }
    if ((flags_[1] & (1 << 0)) != 0) {
      GrpcTagsBinMetadata_ = std::move(other.GrpcTagsBinMetadata_);
    }
    if ((flags_[1] & (1 << 1)) != 0) {
      GrpcTraceBinMetadata_ = std::move(other.GrpcTraceBinMetadata_);
    }
    if ((flags_[1] & (1 << 2)) != 0) {
      HostMetadata_ = std::move(other.HostMetadata_);
    }
    if ((flags_[1] & (1 << 3)) != 0) {
      HttpAuthorityMetadata_ = std::move(other.HttpAuthorityMetadata_);
    }
    if ((flags_[1] & (1 << 4)) != 0) {
      HttpPathMetadata_ = std::move(other.HttpPathMetadata_);
    }
    if ((flags_[1] & (1 << 5)) != 0) {
      LbTokenMetadata_ = std::move(other.LbTokenMetadata_);
    }
    if ((flags_[1] & (1 << 6)) != 0) {
      PeerString_ = std::move(other.PeerString_);
    }
    if ((flags_[1] & (1 << 7)) != 0) {
      UserAgentMetadata_ = std::move(other.UserAgentMetadata_);
    }
    if ((flags_[2] & (1 << 0)) != 0) {
      W3CTraceParentMetadata_ = std::move(other.W3CTraceParentMetadata_);
    }
    if ((flags_[2] & (1 << 1)) != 0) {
      XEnvoyPeerMetadata_ = std::move(other.XEnvoyPeerMetadata_);
    }
    if ((flags_[2] & (1 << 2)) != 0) {
      XForwardedForMetadata_ = std::move(other.XForwardedForMetadata_);
    }
    if ((flags_[2] & (1 << 3)) != 0) {
      XForwardedHostMetadata_ = std::move(other.XForwardedHostMetadata_);
    }
    if ((flags_[2] & (1 << 4)) != 0) {
      GrpcStatusContext_ = std::move(other.GrpcStatusContext_);
    }
    if ((flags_[2] & (1 << 5)) != 0) {
      GrpcRetryPushbackMsMetadata_ = std::move(other.GrpcRetryPushbackMsMetadata_);
    }
    if ((flags_[2] & (1 << 6)) != 0) {
      GrpcTimeoutMetadata_ = std::move(other.GrpcTimeoutMetadata_);
    }
    if ((flags_[2] & (1 << 7)) != 0) {
      GrpcLbClientStatsMetadata_ = std::move(other.GrpcLbClientStatsMetadata_);
    }
    if ((flags_[3] & (1 << 0)) != 0) {
      GrpcRegisteredMethod_ = std::move(other.GrpcRegisteredMethod_);
    }
    if ((flags_[3] & (1 << 1)) != 0) {
      ContentTypeMetadata_ = std::move(other.ContentTypeMetadata_);
    }
    if ((flags_[3] & (1 << 2)) != 0) {
      GrpcAcceptEncodingMetadata_ = std::move(other.GrpcAcceptEncodingMetadata_);
    }
    if ((flags_[3] & (1 << 3)) != 0) {
      GrpcEncodingMetadata_ = std::move(other.GrpcEncodingMetadata_);
    }
    if ((flags_[3] & (1 << 4)) != 0) {
      GrpcInternalEncodingRequest_ = std::move(other.GrpcInternalEncodingRequest_);
    }
    if ((flags_[3] & (1 << 5)) != 0) {
      GrpcPreviousRpcAttemptsMetadata_ = std::move(other.GrpcPreviousRpcAttemptsMetadata_);
    }
    if ((flags_[3] & (1 << 6)) != 0) {
      GrpcStatusMetadata_ = std::move(other.GrpcStatusMetadata_);
    }
    if ((flags_[3] & (1 << 7)) != 0) {
      GrpcStreamNetworkState_ = std::move(other.GrpcStreamNetworkState_);
    }
    if ((flags_[4] & (1 << 0)) != 0) {
      HttpMethodMetadata_ = std::move(other.HttpMethodMetadata_);
    }
    if ((flags_[4] & (1 << 1)) != 0) {
      HttpSchemeMetadata_ = std::move(other.HttpSchemeMetadata_);
    }
    if ((flags_[4] & (1 << 2)) != 0) {
      HttpStatusMetadata_ = std::move(other.HttpStatusMetadata_);
    }
    if ((flags_[4] & (1 << 3)) != 0) {
      LbCostBinMetadata_ = std::move(other.LbCostBinMetadata_);
    }
    if ((flags_[4] & (1 << 4)) != 0) {
      TeMetadata_ = std::move(other.TeMetadata_);
    }
    if ((flags_[4] & (1 << 5)) != 0) {
      WaitForReady_ = std::move(other.WaitForReady_);
    }
    unknown_ = std::move(other.unknown_);
  }
  grpc_metadata_batch& operator=(grpc_metadata_batch&& other) noexcept {
    flags_[0] = other.flags_[0];
    other.flags_[0] = 0;
    // bool_flags_ completely optimized out by FlagAllocator
    flags_[1] = other.flags_[1];
    other.flags_[1] = 0;
    // bool_flags_ completely optimized out by FlagAllocator
    flags_[2] = other.flags_[2];
    other.flags_[2] = 0;
    // bool_flags_ completely optimized out by FlagAllocator
    flags_[3] = other.flags_[3];
    other.flags_[3] = 0;
    // bool_flags_ completely optimized out by FlagAllocator
    flags_[4] = other.flags_[4];
    other.flags_[4] = 0;
    // bool_flags_ completely optimized out by FlagAllocator
    flags_[5] = other.flags_[5];
    other.flags_[5] = 0;
    // bool_flags_ completely optimized out by FlagAllocator
    if ((flags_[0] & (1 << 5)) != 0) {
      EndpointLoadMetricsBinMetadata_ = std::move(other.EndpointLoadMetricsBinMetadata_);
    }
    if ((flags_[0] & (1 << 6)) != 0) {
      GrpcMessageMetadata_ = std::move(other.GrpcMessageMetadata_);
    }
    if ((flags_[0] & (1 << 7)) != 0) {
      GrpcServerStatsBinMetadata_ = std::move(other.GrpcServerStatsBinMetadata_);
    }
    if ((flags_[1] & (1 << 0)) != 0) {
      GrpcTagsBinMetadata_ = std::move(other.GrpcTagsBinMetadata_);
    }
    if ((flags_[1] & (1 << 1)) != 0) {
      GrpcTraceBinMetadata_ = std::move(other.GrpcTraceBinMetadata_);
    }
    if ((flags_[1] & (1 << 2)) != 0) {
      HostMetadata_ = std::move(other.HostMetadata_);
    }
    if ((flags_[1] & (1 << 3)) != 0) {
      HttpAuthorityMetadata_ = std::move(other.HttpAuthorityMetadata_);
    }
    if ((flags_[1] & (1 << 4)) != 0) {
      HttpPathMetadata_ = std::move(other.HttpPathMetadata_);
    }
    if ((flags_[1] & (1 << 5)) != 0) {
      LbTokenMetadata_ = std::move(other.LbTokenMetadata_);
    }
    if ((flags_[1] & (1 << 6)) != 0) {
      PeerString_ = std::move(other.PeerString_);
    }
    if ((flags_[1] & (1 << 7)) != 0) {
      UserAgentMetadata_ = std::move(other.UserAgentMetadata_);
    }
    if ((flags_[2] & (1 << 0)) != 0) {
      W3CTraceParentMetadata_ = std::move(other.W3CTraceParentMetadata_);
    }
    if ((flags_[2] & (1 << 1)) != 0) {
      XEnvoyPeerMetadata_ = std::move(other.XEnvoyPeerMetadata_);
    }
    if ((flags_[2] & (1 << 2)) != 0) {
      XForwardedForMetadata_ = std::move(other.XForwardedForMetadata_);
    }
    if ((flags_[2] & (1 << 3)) != 0) {
      XForwardedHostMetadata_ = std::move(other.XForwardedHostMetadata_);
    }
    if ((flags_[2] & (1 << 4)) != 0) {
      GrpcStatusContext_ = std::move(other.GrpcStatusContext_);
    }
    if ((flags_[2] & (1 << 5)) != 0) {
      GrpcRetryPushbackMsMetadata_ = std::move(other.GrpcRetryPushbackMsMetadata_);
    }
    if ((flags_[2] & (1 << 6)) != 0) {
      GrpcTimeoutMetadata_ = std::move(other.GrpcTimeoutMetadata_);
    }
    if ((flags_[2] & (1 << 7)) != 0) {
      GrpcLbClientStatsMetadata_ = std::move(other.GrpcLbClientStatsMetadata_);
    }
    if ((flags_[3] & (1 << 0)) != 0) {
      GrpcRegisteredMethod_ = std::move(other.GrpcRegisteredMethod_);
    }
    if ((flags_[3] & (1 << 1)) != 0) {
      ContentTypeMetadata_ = std::move(other.ContentTypeMetadata_);
    }
    if ((flags_[3] & (1 << 2)) != 0) {
      GrpcAcceptEncodingMetadata_ = std::move(other.GrpcAcceptEncodingMetadata_);
    }
    if ((flags_[3] & (1 << 3)) != 0) {
      GrpcEncodingMetadata_ = std::move(other.GrpcEncodingMetadata_);
    }
    if ((flags_[3] & (1 << 4)) != 0) {
      GrpcInternalEncodingRequest_ = std::move(other.GrpcInternalEncodingRequest_);
    }
    if ((flags_[3] & (1 << 5)) != 0) {
      GrpcPreviousRpcAttemptsMetadata_ = std::move(other.GrpcPreviousRpcAttemptsMetadata_);
    }
    if ((flags_[3] & (1 << 6)) != 0) {
      GrpcStatusMetadata_ = std::move(other.GrpcStatusMetadata_);
    }
    if ((flags_[3] & (1 << 7)) != 0) {
      GrpcStreamNetworkState_ = std::move(other.GrpcStreamNetworkState_);
    }
    if ((flags_[4] & (1 << 0)) != 0) {
      HttpMethodMetadata_ = std::move(other.HttpMethodMetadata_);
    }
    if ((flags_[4] & (1 << 1)) != 0) {
      HttpSchemeMetadata_ = std::move(other.HttpSchemeMetadata_);
    }
    if ((flags_[4] & (1 << 2)) != 0) {
      HttpStatusMetadata_ = std::move(other.HttpStatusMetadata_);
    }
    if ((flags_[4] & (1 << 3)) != 0) {
      LbCostBinMetadata_ = std::move(other.LbCostBinMetadata_);
    }
    if ((flags_[4] & (1 << 4)) != 0) {
      TeMetadata_ = std::move(other.TeMetadata_);
    }
    if ((flags_[4] & (1 << 5)) != 0) {
      WaitForReady_ = std::move(other.WaitForReady_);
    }
    unknown_ = std::move(other.unknown_);
    return *this;
  }
  template <typename Which>
  auto get(Which) const -> std::optional<typename Which::ValueType> {
    if constexpr (std::is_same_v<Which, GrpcTarPit>) {
      if ((flags_[0] & (1 << 0)) != 0) return std::optional<Empty>(Empty{});
      return std::optional<Empty>();
    }
    if constexpr (std::is_same_v<Which, GrpcCallWasCancelled>) {
      if ((flags_[0] & (1 << 1)) != 0) return std::optional<bool>((flags_[0] & (1 << 1)) != 0);
      return std::optional<bool>();
    }
    if constexpr (std::is_same_v<Which, GrpcStatusFromWire>) {
      if ((flags_[0] & (1 << 2)) != 0) return std::optional<bool>((flags_[0] & (1 << 2)) != 0);
      return std::optional<bool>();
    }
    if constexpr (std::is_same_v<Which, GrpcTrailersOnly>) {
      if ((flags_[0] & (1 << 3)) != 0) return std::optional<bool>((flags_[0] & (1 << 3)) != 0);
      return std::optional<bool>();
    }
    if constexpr (std::is_same_v<Which, IsTransparentRetry>) {
      if ((flags_[0] & (1 << 4)) != 0) return std::optional<bool>((flags_[0] & (1 << 4)) != 0);
      return std::optional<bool>();
    }
    if constexpr (std::is_same_v<Which, EndpointLoadMetricsBinMetadata>) {
      if ((flags_[0] & (1 << 5)) != 0) return std::optional<Slice>(EndpointLoadMetricsBinMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, GrpcMessageMetadata>) {
      if ((flags_[0] & (1 << 6)) != 0) return std::optional<Slice>(GrpcMessageMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, GrpcServerStatsBinMetadata>) {
      if ((flags_[0] & (1 << 7)) != 0) return std::optional<Slice>(GrpcServerStatsBinMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, GrpcTagsBinMetadata>) {
      if ((flags_[1] & (1 << 0)) != 0) return std::optional<Slice>(GrpcTagsBinMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, GrpcTraceBinMetadata>) {
      if ((flags_[1] & (1 << 1)) != 0) return std::optional<Slice>(GrpcTraceBinMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, HostMetadata>) {
      if ((flags_[1] & (1 << 2)) != 0) return std::optional<Slice>(HostMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, HttpAuthorityMetadata>) {
      if ((flags_[1] & (1 << 3)) != 0) return std::optional<Slice>(HttpAuthorityMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, HttpPathMetadata>) {
      if ((flags_[1] & (1 << 4)) != 0) return std::optional<Slice>(HttpPathMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, LbTokenMetadata>) {
      if ((flags_[1] & (1 << 5)) != 0) return std::optional<Slice>(LbTokenMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, PeerString>) {
      if ((flags_[1] & (1 << 6)) != 0) return std::optional<Slice>(PeerString_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, UserAgentMetadata>) {
      if ((flags_[1] & (1 << 7)) != 0) return std::optional<Slice>(UserAgentMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, W3CTraceParentMetadata>) {
      if ((flags_[2] & (1 << 0)) != 0) return std::optional<Slice>(W3CTraceParentMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, XEnvoyPeerMetadata>) {
      if ((flags_[2] & (1 << 1)) != 0) return std::optional<Slice>(XEnvoyPeerMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, XForwardedForMetadata>) {
      if ((flags_[2] & (1 << 2)) != 0) return std::optional<Slice>(XForwardedForMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, XForwardedHostMetadata>) {
      if ((flags_[2] & (1 << 3)) != 0) return std::optional<Slice>(XForwardedHostMetadata_.Copy());
      return std::optional<Slice>();
    }
    if constexpr (std::is_same_v<Which, GrpcStatusContext>) {
      if ((flags_[2] & (1 << 4)) != 0) return std::optional<decltype(GrpcStatusContext_)>(GrpcStatusContext_);
      return std::optional<decltype(GrpcStatusContext_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcRetryPushbackMsMetadata>) {
      if ((flags_[2] & (1 << 5)) != 0) return std::optional<decltype(GrpcRetryPushbackMsMetadata_)>(GrpcRetryPushbackMsMetadata_);
      return std::optional<decltype(GrpcRetryPushbackMsMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcTimeoutMetadata>) {
      if ((flags_[2] & (1 << 6)) != 0) return std::optional<decltype(GrpcTimeoutMetadata_)>(GrpcTimeoutMetadata_);
      return std::optional<decltype(GrpcTimeoutMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcLbClientStatsMetadata>) {
      if ((flags_[2] & (1 << 7)) != 0) return std::optional<decltype(GrpcLbClientStatsMetadata_)>(GrpcLbClientStatsMetadata_);
      return std::optional<decltype(GrpcLbClientStatsMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcRegisteredMethod>) {
      if ((flags_[3] & (1 << 0)) != 0) return std::optional<decltype(GrpcRegisteredMethod_)>(GrpcRegisteredMethod_);
      return std::optional<decltype(GrpcRegisteredMethod_)>();
    }
    if constexpr (std::is_same_v<Which, ContentTypeMetadata>) {
      if ((flags_[3] & (1 << 1)) != 0) return std::optional<decltype(ContentTypeMetadata_)>(ContentTypeMetadata_);
      return std::optional<decltype(ContentTypeMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcAcceptEncodingMetadata>) {
      if ((flags_[3] & (1 << 2)) != 0) return std::optional<decltype(GrpcAcceptEncodingMetadata_)>(GrpcAcceptEncodingMetadata_);
      return std::optional<decltype(GrpcAcceptEncodingMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcEncodingMetadata>) {
      if ((flags_[3] & (1 << 3)) != 0) return std::optional<decltype(GrpcEncodingMetadata_)>(GrpcEncodingMetadata_);
      return std::optional<decltype(GrpcEncodingMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcInternalEncodingRequest>) {
      if ((flags_[3] & (1 << 4)) != 0) return std::optional<decltype(GrpcInternalEncodingRequest_)>(GrpcInternalEncodingRequest_);
      return std::optional<decltype(GrpcInternalEncodingRequest_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcPreviousRpcAttemptsMetadata>) {
      if ((flags_[3] & (1 << 5)) != 0) return std::optional<decltype(GrpcPreviousRpcAttemptsMetadata_)>(GrpcPreviousRpcAttemptsMetadata_);
      return std::optional<decltype(GrpcPreviousRpcAttemptsMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcStatusMetadata>) {
      if ((flags_[3] & (1 << 6)) != 0) return std::optional<decltype(GrpcStatusMetadata_)>(GrpcStatusMetadata_);
      return std::optional<decltype(GrpcStatusMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, GrpcStreamNetworkState>) {
      if ((flags_[3] & (1 << 7)) != 0) return std::optional<decltype(GrpcStreamNetworkState_)>(GrpcStreamNetworkState_);
      return std::optional<decltype(GrpcStreamNetworkState_)>();
    }
    if constexpr (std::is_same_v<Which, HttpMethodMetadata>) {
      if ((flags_[4] & (1 << 0)) != 0) return std::optional<decltype(HttpMethodMetadata_)>(HttpMethodMetadata_);
      return std::optional<decltype(HttpMethodMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, HttpSchemeMetadata>) {
      if ((flags_[4] & (1 << 1)) != 0) return std::optional<decltype(HttpSchemeMetadata_)>(HttpSchemeMetadata_);
      return std::optional<decltype(HttpSchemeMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, HttpStatusMetadata>) {
      if ((flags_[4] & (1 << 2)) != 0) return std::optional<decltype(HttpStatusMetadata_)>(HttpStatusMetadata_);
      return std::optional<decltype(HttpStatusMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, LbCostBinMetadata>) {
      if ((flags_[4] & (1 << 3)) != 0) return std::optional<decltype(LbCostBinMetadata_)>(LbCostBinMetadata_);
      return std::optional<decltype(LbCostBinMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, TeMetadata>) {
      if ((flags_[4] & (1 << 4)) != 0) return std::optional<decltype(TeMetadata_)>(TeMetadata_);
      return std::optional<decltype(TeMetadata_)>();
    }
    if constexpr (std::is_same_v<Which, WaitForReady>) {
      if ((flags_[4] & (1 << 5)) != 0) return std::optional<decltype(WaitForReady_)>(WaitForReady_);
      return std::optional<decltype(WaitForReady_)>();
    }
    return std::nullopt;
  }
  friend bool IsStatusOk(const grpc_metadata_batch& m) {
    return m.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN) == GRPC_STATUS_OK;
  }

  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>, const Empty*> {
    static const Empty empty;
    if ((flags_[0] & (1 << 0)) != 0) return &empty;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>, Empty*> {
    static Empty empty;
    if ((flags_[0] & (1 << 0)) != 0) return &empty;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>, Empty*> {
    flags_[0] |= (1 << 0);
    static Empty empty;
    return &empty;
  }
  template <typename Trait>
  auto set(Trait, Empty) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>> {
    flags_[0] |= (1 << 0);
  }
  template <typename Trait>
  auto Set(Trait, Empty) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>> {
    flags_[0] |= (1 << 0);
  }
  template <typename Trait>
  auto Set(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>> {
    flags_[0] |= (1 << 0);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTarPit>> {
    flags_[0] &= ~(1 << 0);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcCallWasCancelled>, const bool*> {
    static const bool t = true;
    if ((flags_[0] & (1 << 1)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcCallWasCancelled>, bool*> {
    static bool t = true;
    if ((flags_[0] & (1 << 1)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcCallWasCancelled>, bool*> {
    flags_[0] |= (1 << 1);
    static bool t = true;
    return &t;
  }
  template <typename Trait>
  auto set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcCallWasCancelled>> {
    if (v) flags_[0] |= (1 << 1);
    else flags_[0] &= ~(1 << 1);
  }
  template <typename Trait>
  auto Set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcCallWasCancelled>> {
    if (v) flags_[0] |= (1 << 1);
    else flags_[0] &= ~(1 << 1);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcCallWasCancelled>> {
    flags_[0] &= ~(1 << 1);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusFromWire>, const bool*> {
    static const bool t = true;
    if ((flags_[0] & (1 << 2)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusFromWire>, bool*> {
    static bool t = true;
    if ((flags_[0] & (1 << 2)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusFromWire>, bool*> {
    flags_[0] |= (1 << 2);
    static bool t = true;
    return &t;
  }
  template <typename Trait>
  auto set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusFromWire>> {
    if (v) flags_[0] |= (1 << 2);
    else flags_[0] &= ~(1 << 2);
  }
  template <typename Trait>
  auto Set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusFromWire>> {
    if (v) flags_[0] |= (1 << 2);
    else flags_[0] &= ~(1 << 2);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusFromWire>> {
    flags_[0] &= ~(1 << 2);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcTrailersOnly>, const bool*> {
    static const bool t = true;
    if ((flags_[0] & (1 << 3)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTrailersOnly>, bool*> {
    static bool t = true;
    if ((flags_[0] & (1 << 3)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTrailersOnly>, bool*> {
    flags_[0] |= (1 << 3);
    static bool t = true;
    return &t;
  }
  template <typename Trait>
  auto set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTrailersOnly>> {
    if (v) flags_[0] |= (1 << 3);
    else flags_[0] &= ~(1 << 3);
  }
  template <typename Trait>
  auto Set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTrailersOnly>> {
    if (v) flags_[0] |= (1 << 3);
    else flags_[0] &= ~(1 << 3);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTrailersOnly>> {
    flags_[0] &= ~(1 << 3);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, IsTransparentRetry>, const bool*> {
    static const bool t = true;
    if ((flags_[0] & (1 << 4)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, IsTransparentRetry>, bool*> {
    static bool t = true;
    if ((flags_[0] & (1 << 4)) != 0) return &t;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, IsTransparentRetry>, bool*> {
    flags_[0] |= (1 << 4);
    static bool t = true;
    return &t;
  }
  template <typename Trait>
  auto set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, IsTransparentRetry>> {
    if (v) flags_[0] |= (1 << 4);
    else flags_[0] &= ~(1 << 4);
  }
  template <typename Trait>
  auto Set(Trait, bool v) -> absl::enable_if_t<std::is_same_v<Trait, IsTransparentRetry>> {
    if (v) flags_[0] |= (1 << 4);
    else flags_[0] &= ~(1 << 4);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, IsTransparentRetry>> {
    flags_[0] &= ~(1 << 4);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, EndpointLoadMetricsBinMetadata>, const Slice*> {
    if ((flags_[0] & (1 << 5)) != 0) return &EndpointLoadMetricsBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, EndpointLoadMetricsBinMetadata>, Slice*> {
    if ((flags_[0] & (1 << 5)) != 0) return &EndpointLoadMetricsBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, EndpointLoadMetricsBinMetadata>, Slice*> {
    flags_[0] |= (1 << 5);
    return &EndpointLoadMetricsBinMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, EndpointLoadMetricsBinMetadata>> {
    flags_[0] |= (1 << 5);
    EndpointLoadMetricsBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, EndpointLoadMetricsBinMetadata>> {
    flags_[0] |= (1 << 5);
    EndpointLoadMetricsBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, EndpointLoadMetricsBinMetadata>> {
    flags_[0] &= ~(1 << 5);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcMessageMetadata>, const Slice*> {
    if ((flags_[0] & (1 << 6)) != 0) return &GrpcMessageMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcMessageMetadata>, Slice*> {
    if ((flags_[0] & (1 << 6)) != 0) return &GrpcMessageMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcMessageMetadata>, Slice*> {
    flags_[0] |= (1 << 6);
    return &GrpcMessageMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcMessageMetadata>> {
    flags_[0] |= (1 << 6);
    GrpcMessageMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcMessageMetadata>> {
    flags_[0] |= (1 << 6);
    GrpcMessageMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcMessageMetadata>> {
    flags_[0] &= ~(1 << 6);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcServerStatsBinMetadata>, const Slice*> {
    if ((flags_[0] & (1 << 7)) != 0) return &GrpcServerStatsBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcServerStatsBinMetadata>, Slice*> {
    if ((flags_[0] & (1 << 7)) != 0) return &GrpcServerStatsBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcServerStatsBinMetadata>, Slice*> {
    flags_[0] |= (1 << 7);
    return &GrpcServerStatsBinMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcServerStatsBinMetadata>> {
    flags_[0] |= (1 << 7);
    GrpcServerStatsBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcServerStatsBinMetadata>> {
    flags_[0] |= (1 << 7);
    GrpcServerStatsBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcServerStatsBinMetadata>> {
    flags_[0] &= ~(1 << 7);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcTagsBinMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 0)) != 0) return &GrpcTagsBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTagsBinMetadata>, Slice*> {
    if ((flags_[1] & (1 << 0)) != 0) return &GrpcTagsBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTagsBinMetadata>, Slice*> {
    flags_[1] |= (1 << 0);
    return &GrpcTagsBinMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTagsBinMetadata>> {
    flags_[1] |= (1 << 0);
    GrpcTagsBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTagsBinMetadata>> {
    flags_[1] |= (1 << 0);
    GrpcTagsBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTagsBinMetadata>> {
    flags_[1] &= ~(1 << 0);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcTraceBinMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 1)) != 0) return &GrpcTraceBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTraceBinMetadata>, Slice*> {
    if ((flags_[1] & (1 << 1)) != 0) return &GrpcTraceBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTraceBinMetadata>, Slice*> {
    flags_[1] |= (1 << 1);
    return &GrpcTraceBinMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTraceBinMetadata>> {
    flags_[1] |= (1 << 1);
    GrpcTraceBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTraceBinMetadata>> {
    flags_[1] |= (1 << 1);
    GrpcTraceBinMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTraceBinMetadata>> {
    flags_[1] &= ~(1 << 1);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, HostMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 2)) != 0) return &HostMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HostMetadata>, Slice*> {
    if ((flags_[1] & (1 << 2)) != 0) return &HostMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HostMetadata>, Slice*> {
    flags_[1] |= (1 << 2);
    return &HostMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HostMetadata>> {
    flags_[1] |= (1 << 2);
    HostMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HostMetadata>> {
    flags_[1] |= (1 << 2);
    HostMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HostMetadata>> {
    flags_[1] &= ~(1 << 2);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, HttpAuthorityMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 3)) != 0) return &HttpAuthorityMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpAuthorityMetadata>, Slice*> {
    if ((flags_[1] & (1 << 3)) != 0) return &HttpAuthorityMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpAuthorityMetadata>, Slice*> {
    flags_[1] |= (1 << 3);
    return &HttpAuthorityMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpAuthorityMetadata>> {
    flags_[1] |= (1 << 3);
    HttpAuthorityMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpAuthorityMetadata>> {
    flags_[1] |= (1 << 3);
    HttpAuthorityMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpAuthorityMetadata>> {
    flags_[1] &= ~(1 << 3);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, HttpPathMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 4)) != 0) return &HttpPathMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpPathMetadata>, Slice*> {
    if ((flags_[1] & (1 << 4)) != 0) return &HttpPathMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpPathMetadata>, Slice*> {
    flags_[1] |= (1 << 4);
    return &HttpPathMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpPathMetadata>> {
    flags_[1] |= (1 << 4);
    HttpPathMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpPathMetadata>> {
    flags_[1] |= (1 << 4);
    HttpPathMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpPathMetadata>> {
    flags_[1] &= ~(1 << 4);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, LbTokenMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 5)) != 0) return &LbTokenMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, LbTokenMetadata>, Slice*> {
    if ((flags_[1] & (1 << 5)) != 0) return &LbTokenMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, LbTokenMetadata>, Slice*> {
    flags_[1] |= (1 << 5);
    return &LbTokenMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, LbTokenMetadata>> {
    flags_[1] |= (1 << 5);
    LbTokenMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, LbTokenMetadata>> {
    flags_[1] |= (1 << 5);
    LbTokenMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, LbTokenMetadata>> {
    flags_[1] &= ~(1 << 5);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, PeerString>, const Slice*> {
    if ((flags_[1] & (1 << 6)) != 0) return &PeerString_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, PeerString>, Slice*> {
    if ((flags_[1] & (1 << 6)) != 0) return &PeerString_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, PeerString>, Slice*> {
    flags_[1] |= (1 << 6);
    return &PeerString_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, PeerString>> {
    flags_[1] |= (1 << 6);
    PeerString_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, PeerString>> {
    flags_[1] |= (1 << 6);
    PeerString_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, PeerString>> {
    flags_[1] &= ~(1 << 6);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, UserAgentMetadata>, const Slice*> {
    if ((flags_[1] & (1 << 7)) != 0) return &UserAgentMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, UserAgentMetadata>, Slice*> {
    if ((flags_[1] & (1 << 7)) != 0) return &UserAgentMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, UserAgentMetadata>, Slice*> {
    flags_[1] |= (1 << 7);
    return &UserAgentMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, UserAgentMetadata>> {
    flags_[1] |= (1 << 7);
    UserAgentMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, UserAgentMetadata>> {
    flags_[1] |= (1 << 7);
    UserAgentMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, UserAgentMetadata>> {
    flags_[1] &= ~(1 << 7);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, W3CTraceParentMetadata>, const Slice*> {
    if ((flags_[2] & (1 << 0)) != 0) return &W3CTraceParentMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, W3CTraceParentMetadata>, Slice*> {
    if ((flags_[2] & (1 << 0)) != 0) return &W3CTraceParentMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, W3CTraceParentMetadata>, Slice*> {
    flags_[2] |= (1 << 0);
    return &W3CTraceParentMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, W3CTraceParentMetadata>> {
    flags_[2] |= (1 << 0);
    W3CTraceParentMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, W3CTraceParentMetadata>> {
    flags_[2] |= (1 << 0);
    W3CTraceParentMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, W3CTraceParentMetadata>> {
    flags_[2] &= ~(1 << 0);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, XEnvoyPeerMetadata>, const Slice*> {
    if ((flags_[2] & (1 << 1)) != 0) return &XEnvoyPeerMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XEnvoyPeerMetadata>, Slice*> {
    if ((flags_[2] & (1 << 1)) != 0) return &XEnvoyPeerMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XEnvoyPeerMetadata>, Slice*> {
    flags_[2] |= (1 << 1);
    return &XEnvoyPeerMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, XEnvoyPeerMetadata>> {
    flags_[2] |= (1 << 1);
    XEnvoyPeerMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, XEnvoyPeerMetadata>> {
    flags_[2] |= (1 << 1);
    XEnvoyPeerMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XEnvoyPeerMetadata>> {
    flags_[2] &= ~(1 << 1);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, XForwardedForMetadata>, const Slice*> {
    if ((flags_[2] & (1 << 2)) != 0) return &XForwardedForMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedForMetadata>, Slice*> {
    if ((flags_[2] & (1 << 2)) != 0) return &XForwardedForMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedForMetadata>, Slice*> {
    flags_[2] |= (1 << 2);
    return &XForwardedForMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedForMetadata>> {
    flags_[2] |= (1 << 2);
    XForwardedForMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedForMetadata>> {
    flags_[2] |= (1 << 2);
    XForwardedForMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedForMetadata>> {
    flags_[2] &= ~(1 << 2);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, XForwardedHostMetadata>, const Slice*> {
    if ((flags_[2] & (1 << 3)) != 0) return &XForwardedHostMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedHostMetadata>, Slice*> {
    if ((flags_[2] & (1 << 3)) != 0) return &XForwardedHostMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedHostMetadata>, Slice*> {
    flags_[2] |= (1 << 3);
    return &XForwardedHostMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedHostMetadata>> {
    flags_[2] |= (1 << 3);
    XForwardedHostMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedHostMetadata>> {
    flags_[2] |= (1 << 3);
    XForwardedHostMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, XForwardedHostMetadata>> {
    flags_[2] &= ~(1 << 3);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusContext>, const absl::InlinedVector<std::string, 1>*> {
    if ((flags_[2] & (1 << 4)) != 0) return &GrpcStatusContext_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusContext>, absl::InlinedVector<std::string, 1>*> {
    if ((flags_[2] & (1 << 4)) != 0) return &GrpcStatusContext_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusContext>, absl::InlinedVector<std::string, 1>*> {
    flags_[2] |= (1 << 4);
    return &GrpcStatusContext_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusContext>> {
    flags_[2] |= (1 << 4);
    GrpcStatusContext_.push_back(std::forward<V>(v));
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusContext>> {
    flags_[2] |= (1 << 4);
    GrpcStatusContext_.push_back(std::forward<V>(v));
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusContext>> {
    flags_[2] &= ~(1 << 4);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcRetryPushbackMsMetadata>, const Duration*> {
    if ((flags_[2] & (1 << 5)) != 0) return &GrpcRetryPushbackMsMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRetryPushbackMsMetadata>, Duration*> {
    if ((flags_[2] & (1 << 5)) != 0) return &GrpcRetryPushbackMsMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRetryPushbackMsMetadata>, Duration*> {
    flags_[2] |= (1 << 5);
    return &GrpcRetryPushbackMsMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRetryPushbackMsMetadata>> {
    flags_[2] |= (1 << 5);
    GrpcRetryPushbackMsMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRetryPushbackMsMetadata>> {
    flags_[2] |= (1 << 5);
    GrpcRetryPushbackMsMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRetryPushbackMsMetadata>> {
    flags_[2] &= ~(1 << 5);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcTimeoutMetadata>, const Timestamp*> {
    if ((flags_[2] & (1 << 6)) != 0) return &GrpcTimeoutMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTimeoutMetadata>, Timestamp*> {
    if ((flags_[2] & (1 << 6)) != 0) return &GrpcTimeoutMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTimeoutMetadata>, Timestamp*> {
    flags_[2] |= (1 << 6);
    return &GrpcTimeoutMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTimeoutMetadata>> {
    flags_[2] |= (1 << 6);
    GrpcTimeoutMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTimeoutMetadata>> {
    flags_[2] |= (1 << 6);
    GrpcTimeoutMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcTimeoutMetadata>> {
    flags_[2] &= ~(1 << 6);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcLbClientStatsMetadata>, const GrpcLbClientStats**> {
    if ((flags_[2] & (1 << 7)) != 0) return &GrpcLbClientStatsMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcLbClientStatsMetadata>, GrpcLbClientStats**> {
    if ((flags_[2] & (1 << 7)) != 0) return &GrpcLbClientStatsMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcLbClientStatsMetadata>, GrpcLbClientStats**> {
    flags_[2] |= (1 << 7);
    return &GrpcLbClientStatsMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcLbClientStatsMetadata>> {
    flags_[2] |= (1 << 7);
    GrpcLbClientStatsMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcLbClientStatsMetadata>> {
    flags_[2] |= (1 << 7);
    GrpcLbClientStatsMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcLbClientStatsMetadata>> {
    flags_[2] &= ~(1 << 7);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcRegisteredMethod>, const void**> {
    if ((flags_[3] & (1 << 0)) != 0) return &GrpcRegisteredMethod_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRegisteredMethod>, void**> {
    if ((flags_[3] & (1 << 0)) != 0) return &GrpcRegisteredMethod_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRegisteredMethod>, void**> {
    flags_[3] |= (1 << 0);
    return &GrpcRegisteredMethod_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRegisteredMethod>> {
    flags_[3] |= (1 << 0);
    GrpcRegisteredMethod_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRegisteredMethod>> {
    flags_[3] |= (1 << 0);
    GrpcRegisteredMethod_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcRegisteredMethod>> {
    flags_[3] &= ~(1 << 0);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, ContentTypeMetadata>, const ContentTypeMetadata::ValueType*> {
    if ((flags_[3] & (1 << 1)) != 0) return &ContentTypeMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, ContentTypeMetadata>, ContentTypeMetadata::ValueType*> {
    if ((flags_[3] & (1 << 1)) != 0) return &ContentTypeMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, ContentTypeMetadata>, ContentTypeMetadata::ValueType*> {
    flags_[3] |= (1 << 1);
    return &ContentTypeMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, ContentTypeMetadata>> {
    flags_[3] |= (1 << 1);
    ContentTypeMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, ContentTypeMetadata>> {
    flags_[3] |= (1 << 1);
    ContentTypeMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, ContentTypeMetadata>> {
    flags_[3] &= ~(1 << 1);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcAcceptEncodingMetadata>, const CompressionAlgorithmSet*> {
    if ((flags_[3] & (1 << 2)) != 0) return &GrpcAcceptEncodingMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcAcceptEncodingMetadata>, CompressionAlgorithmSet*> {
    if ((flags_[3] & (1 << 2)) != 0) return &GrpcAcceptEncodingMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcAcceptEncodingMetadata>, CompressionAlgorithmSet*> {
    flags_[3] |= (1 << 2);
    return &GrpcAcceptEncodingMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcAcceptEncodingMetadata>> {
    flags_[3] |= (1 << 2);
    GrpcAcceptEncodingMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcAcceptEncodingMetadata>> {
    flags_[3] |= (1 << 2);
    GrpcAcceptEncodingMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcAcceptEncodingMetadata>> {
    flags_[3] &= ~(1 << 2);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcEncodingMetadata>, const grpc_compression_algorithm*> {
    if ((flags_[3] & (1 << 3)) != 0) return &GrpcEncodingMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcEncodingMetadata>, grpc_compression_algorithm*> {
    if ((flags_[3] & (1 << 3)) != 0) return &GrpcEncodingMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcEncodingMetadata>, grpc_compression_algorithm*> {
    flags_[3] |= (1 << 3);
    return &GrpcEncodingMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcEncodingMetadata>> {
    flags_[3] |= (1 << 3);
    GrpcEncodingMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcEncodingMetadata>> {
    flags_[3] |= (1 << 3);
    GrpcEncodingMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcEncodingMetadata>> {
    flags_[3] &= ~(1 << 3);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcInternalEncodingRequest>, const grpc_compression_algorithm*> {
    if ((flags_[3] & (1 << 4)) != 0) return &GrpcInternalEncodingRequest_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcInternalEncodingRequest>, grpc_compression_algorithm*> {
    if ((flags_[3] & (1 << 4)) != 0) return &GrpcInternalEncodingRequest_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcInternalEncodingRequest>, grpc_compression_algorithm*> {
    flags_[3] |= (1 << 4);
    return &GrpcInternalEncodingRequest_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcInternalEncodingRequest>> {
    flags_[3] |= (1 << 4);
    GrpcInternalEncodingRequest_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcInternalEncodingRequest>> {
    flags_[3] |= (1 << 4);
    GrpcInternalEncodingRequest_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcInternalEncodingRequest>> {
    flags_[3] &= ~(1 << 4);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcPreviousRpcAttemptsMetadata>, const uint32_t*> {
    if ((flags_[3] & (1 << 5)) != 0) return &GrpcPreviousRpcAttemptsMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcPreviousRpcAttemptsMetadata>, uint32_t*> {
    if ((flags_[3] & (1 << 5)) != 0) return &GrpcPreviousRpcAttemptsMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcPreviousRpcAttemptsMetadata>, uint32_t*> {
    flags_[3] |= (1 << 5);
    return &GrpcPreviousRpcAttemptsMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcPreviousRpcAttemptsMetadata>> {
    flags_[3] |= (1 << 5);
    GrpcPreviousRpcAttemptsMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcPreviousRpcAttemptsMetadata>> {
    flags_[3] |= (1 << 5);
    GrpcPreviousRpcAttemptsMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcPreviousRpcAttemptsMetadata>> {
    flags_[3] &= ~(1 << 5);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusMetadata>, const grpc_status_code*> {
    if ((flags_[3] & (1 << 6)) != 0) return &GrpcStatusMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusMetadata>, grpc_status_code*> {
    if ((flags_[3] & (1 << 6)) != 0) return &GrpcStatusMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusMetadata>, grpc_status_code*> {
    flags_[3] |= (1 << 6);
    return &GrpcStatusMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusMetadata>> {
    flags_[3] |= (1 << 6);
    GrpcStatusMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusMetadata>> {
    flags_[3] |= (1 << 6);
    GrpcStatusMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStatusMetadata>> {
    flags_[3] &= ~(1 << 6);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, GrpcStreamNetworkState>, const GrpcStreamNetworkState::ValueType*> {
    if ((flags_[3] & (1 << 7)) != 0) return &GrpcStreamNetworkState_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStreamNetworkState>, GrpcStreamNetworkState::ValueType*> {
    if ((flags_[3] & (1 << 7)) != 0) return &GrpcStreamNetworkState_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStreamNetworkState>, GrpcStreamNetworkState::ValueType*> {
    flags_[3] |= (1 << 7);
    return &GrpcStreamNetworkState_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStreamNetworkState>> {
    flags_[3] |= (1 << 7);
    GrpcStreamNetworkState_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStreamNetworkState>> {
    flags_[3] |= (1 << 7);
    GrpcStreamNetworkState_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, GrpcStreamNetworkState>> {
    flags_[3] &= ~(1 << 7);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, HttpMethodMetadata>, const HttpMethodMetadata::ValueType*> {
    if ((flags_[4] & (1 << 0)) != 0) return &HttpMethodMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpMethodMetadata>, HttpMethodMetadata::ValueType*> {
    if ((flags_[4] & (1 << 0)) != 0) return &HttpMethodMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpMethodMetadata>, HttpMethodMetadata::ValueType*> {
    flags_[4] |= (1 << 0);
    return &HttpMethodMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpMethodMetadata>> {
    flags_[4] |= (1 << 0);
    HttpMethodMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpMethodMetadata>> {
    flags_[4] |= (1 << 0);
    HttpMethodMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpMethodMetadata>> {
    flags_[4] &= ~(1 << 0);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, HttpSchemeMetadata>, const HttpSchemeMetadata::ValueType*> {
    if ((flags_[4] & (1 << 1)) != 0) return &HttpSchemeMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpSchemeMetadata>, HttpSchemeMetadata::ValueType*> {
    if ((flags_[4] & (1 << 1)) != 0) return &HttpSchemeMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpSchemeMetadata>, HttpSchemeMetadata::ValueType*> {
    flags_[4] |= (1 << 1);
    return &HttpSchemeMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpSchemeMetadata>> {
    flags_[4] |= (1 << 1);
    HttpSchemeMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpSchemeMetadata>> {
    flags_[4] |= (1 << 1);
    HttpSchemeMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpSchemeMetadata>> {
    flags_[4] &= ~(1 << 1);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, HttpStatusMetadata>, const uint32_t*> {
    if ((flags_[4] & (1 << 2)) != 0) return &HttpStatusMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpStatusMetadata>, uint32_t*> {
    if ((flags_[4] & (1 << 2)) != 0) return &HttpStatusMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpStatusMetadata>, uint32_t*> {
    flags_[4] |= (1 << 2);
    return &HttpStatusMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpStatusMetadata>> {
    flags_[4] |= (1 << 2);
    HttpStatusMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, HttpStatusMetadata>> {
    flags_[4] |= (1 << 2);
    HttpStatusMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, HttpStatusMetadata>> {
    flags_[4] &= ~(1 << 2);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, LbCostBinMetadata>, const absl::InlinedVector<LbCostBinMetadata::ValueType, 1>*> {
    if ((flags_[4] & (1 << 3)) != 0) return &LbCostBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, LbCostBinMetadata>, absl::InlinedVector<LbCostBinMetadata::ValueType, 1>*> {
    if ((flags_[4] & (1 << 3)) != 0) return &LbCostBinMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, LbCostBinMetadata>, absl::InlinedVector<LbCostBinMetadata::ValueType, 1>*> {
    flags_[4] |= (1 << 3);
    return &LbCostBinMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, LbCostBinMetadata>> {
    flags_[4] |= (1 << 3);
    LbCostBinMetadata_.push_back(std::forward<V>(v));
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, LbCostBinMetadata>> {
    flags_[4] |= (1 << 3);
    LbCostBinMetadata_.push_back(std::forward<V>(v));
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, LbCostBinMetadata>> {
    flags_[4] &= ~(1 << 3);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, TeMetadata>, const TeMetadata::ValueType*> {
    if ((flags_[4] & (1 << 4)) != 0) return &TeMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, TeMetadata>, TeMetadata::ValueType*> {
    if ((flags_[4] & (1 << 4)) != 0) return &TeMetadata_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, TeMetadata>, TeMetadata::ValueType*> {
    flags_[4] |= (1 << 4);
    return &TeMetadata_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, TeMetadata>> {
    flags_[4] |= (1 << 4);
    TeMetadata_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, TeMetadata>> {
    flags_[4] |= (1 << 4);
    TeMetadata_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, TeMetadata>> {
    flags_[4] &= ~(1 << 4);
  }


  template <typename Trait>
  auto get_pointer(Trait) const -> absl::enable_if_t<std::is_same_v<Trait, WaitForReady>, const WaitForReady::ValueType*> {
    if ((flags_[4] & (1 << 5)) != 0) return &WaitForReady_;
    return nullptr;
  }
  template <typename Trait>
  auto get_pointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, WaitForReady>, WaitForReady::ValueType*> {
    if ((flags_[4] & (1 << 5)) != 0) return &WaitForReady_;
    return nullptr;
  }
  template <typename Trait>
  auto GetOrCreatePointer(Trait) -> absl::enable_if_t<std::is_same_v<Trait, WaitForReady>, WaitForReady::ValueType*> {
    flags_[4] |= (1 << 5);
    return &WaitForReady_;
  }


  template <typename Trait, typename V>
  auto set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, WaitForReady>> {
    flags_[4] |= (1 << 5);
    WaitForReady_ = std::forward<V>(v);
  }
  template <typename Trait, typename V>
  auto Set(Trait, V&& v) -> absl::enable_if_t<std::is_same_v<Trait, WaitForReady>> {
    flags_[4] |= (1 << 5);
    WaitForReady_ = std::forward<V>(v);
  }
  template <typename Trait>
  auto Remove(Trait) -> absl::enable_if_t<std::is_same_v<Trait, WaitForReady>> {
    flags_[4] &= ~(1 << 5);
  }


  template <typename Trait>
  auto Take(Trait t) -> absl::enable_if_t<!std::is_same_v<typename Trait::ValueType, Slice> && !Trait::kRepeatable, std::optional<typename Trait::ValueType>> {
    if (auto* p = get_pointer(t)) {
      auto val = std::move(*p);
      Remove(t);
      return val;
    }
    return std::nullopt;
  }
  template <typename Trait>
  auto Take(Trait t) -> absl::enable_if_t<std::is_same_v<typename Trait::ValueType, Slice> && !Trait::kRepeatable, std::optional<Slice>> {
    if (auto* p = get_pointer(t)) {
      auto val = std::move(*p);
      Remove(t);
      return val;
    }
    return std::nullopt;
  }
  template <typename Trait>
  auto Take(Trait t) -> absl::enable_if_t<Trait::kRepeatable, absl::InlinedVector<typename Trait::ValueType, 1>> {
    if (auto* p = get_pointer(t)) {
      auto val = std::move(*p);
      Remove(t);
      return val;
    }
    return {};
  }

  void Set(const ParsedMetadata<grpc_metadata_batch>&) {}
  size_t TransportSize() const { return 0; }
  
  template <typename Encoder> void Encode(Encoder* encoder) const {

    if ((flags_[0] & (1 << 5)) != 0) encoder->Encode(EndpointLoadMetricsBinMetadata(), EndpointLoadMetricsBinMetadata_);
    if ((flags_[0] & (1 << 6)) != 0) encoder->Encode(GrpcMessageMetadata(), GrpcMessageMetadata_);
    if ((flags_[0] & (1 << 7)) != 0) encoder->Encode(GrpcServerStatsBinMetadata(), GrpcServerStatsBinMetadata_);
    if ((flags_[1] & (1 << 0)) != 0) encoder->Encode(GrpcTagsBinMetadata(), GrpcTagsBinMetadata_);
    if ((flags_[1] & (1 << 1)) != 0) encoder->Encode(GrpcTraceBinMetadata(), GrpcTraceBinMetadata_);
    if ((flags_[1] & (1 << 2)) != 0) encoder->Encode(HostMetadata(), HostMetadata_);
    if ((flags_[1] & (1 << 3)) != 0) encoder->Encode(HttpAuthorityMetadata(), HttpAuthorityMetadata_);
    if ((flags_[1] & (1 << 4)) != 0) encoder->Encode(HttpPathMetadata(), HttpPathMetadata_);
    if ((flags_[1] & (1 << 5)) != 0) encoder->Encode(LbTokenMetadata(), LbTokenMetadata_);
    if ((flags_[1] & (1 << 7)) != 0) encoder->Encode(UserAgentMetadata(), UserAgentMetadata_);
    if ((flags_[2] & (1 << 0)) != 0) encoder->Encode(W3CTraceParentMetadata(), W3CTraceParentMetadata_);
    if ((flags_[2] & (1 << 1)) != 0) encoder->Encode(XEnvoyPeerMetadata(), XEnvoyPeerMetadata_);
    if ((flags_[2] & (1 << 2)) != 0) encoder->Encode(XForwardedForMetadata(), XForwardedForMetadata_);
    if ((flags_[2] & (1 << 3)) != 0) encoder->Encode(XForwardedHostMetadata(), XForwardedHostMetadata_);
    if ((flags_[2] & (1 << 5)) != 0) encoder->Encode(GrpcRetryPushbackMsMetadata(), GrpcRetryPushbackMsMetadata_);
    if ((flags_[2] & (1 << 6)) != 0) encoder->Encode(GrpcTimeoutMetadata(), GrpcTimeoutMetadata_);
    if ((flags_[2] & (1 << 7)) != 0) encoder->Encode(GrpcLbClientStatsMetadata(), GrpcLbClientStatsMetadata_);
    if ((flags_[3] & (1 << 1)) != 0) encoder->Encode(ContentTypeMetadata(), ContentTypeMetadata_);
    if ((flags_[3] & (1 << 2)) != 0) encoder->Encode(GrpcAcceptEncodingMetadata(), GrpcAcceptEncodingMetadata_);
    if ((flags_[3] & (1 << 3)) != 0) encoder->Encode(GrpcEncodingMetadata(), GrpcEncodingMetadata_);
    if ((flags_[3] & (1 << 4)) != 0) encoder->Encode(GrpcInternalEncodingRequest(), GrpcInternalEncodingRequest_);
    if ((flags_[3] & (1 << 5)) != 0) encoder->Encode(GrpcPreviousRpcAttemptsMetadata(), GrpcPreviousRpcAttemptsMetadata_);
    if ((flags_[4] & (1 << 0)) != 0) encoder->Encode(HttpMethodMetadata(), HttpMethodMetadata_);
    if ((flags_[4] & (1 << 1)) != 0) encoder->Encode(HttpSchemeMetadata(), HttpSchemeMetadata_);
    if ((flags_[4] & (1 << 2)) != 0) encoder->Encode(HttpStatusMetadata(), HttpStatusMetadata_);
    if ((flags_[4] & (1 << 3)) != 0) {
      for (const auto& v : LbCostBinMetadata_) encoder->Encode(LbCostBinMetadata(), v);
    }
    if ((flags_[4] & (1 << 4)) != 0) encoder->Encode(TeMetadata(), TeMetadata_);
    for (const auto& p : unknown_.unknown()) {
      encoder->Encode(p.first, p.second);
    }
  }
  
  static ParsedMetadata<grpc_metadata_batch> Parse(absl::string_view, Slice, bool, uint32_t, MetadataParseErrorFn) {
    return ParsedMetadata<grpc_metadata_batch>();
  }
  
  grpc_metadata_batch Copy() const {
    grpc_metadata_batch c;
    for(size_t i = 0; i < sizeof(flags_); i++) c.flags_[i] = flags_[i];
    // Note: proper copying for slice and strings would go here
    for (const auto& p : unknown_.unknown()) {
      c.unknown_.Append(p.first.as_string_view(), p.second.Copy());
    }
    return c;
  }

  std::optional<absl::string_view> GetStringValue(absl::string_view key, std::string* buffer) const {
    return unknown_.GetStringValue(key, buffer);
  }

  void Remove(absl::string_view key) { unknown_.Remove(key); }
  void Clear() {
    for(size_t i = 0; i < sizeof(flags_); i++) flags_[i] = 0;
    unknown_.Clear();
  }

  size_t count() const {
    size_t c = 0;
    for (auto b : flags_) c += absl::popcount(b);
    return c + unknown_.size();
  }

  void Append(absl::string_view key, Slice value, MetadataParseErrorFn on_error) {

    if (key == "endpoint-load-metrics-bin") {
      Set(EndpointLoadMetricsBinMetadata(), value.Copy());
      return;
    }
    if (key == "grpc-message") {
      Set(GrpcMessageMetadata(), value.Copy());
      return;
    }
    if (key == "grpc-server-stats-bin") {
      Set(GrpcServerStatsBinMetadata(), value.Copy());
      return;
    }
    if (key == "grpc-tags-bin") {
      Set(GrpcTagsBinMetadata(), value.Copy());
      return;
    }
    if (key == "grpc-trace-bin") {
      Set(GrpcTraceBinMetadata(), value.Copy());
      return;
    }
    if (key == "host") {
      Set(HostMetadata(), value.Copy());
      return;
    }
    if (key == ":authority") {
      Set(HttpAuthorityMetadata(), value.Copy());
      return;
    }
    if (key == ":path") {
      Set(HttpPathMetadata(), value.Copy());
      return;
    }
    if (key == "lb-token") {
      Set(LbTokenMetadata(), value.Copy());
      return;
    }
    if (key == "user-agent") {
      Set(UserAgentMetadata(), value.Copy());
      return;
    }
    if (key == "traceparent") {
      Set(W3CTraceParentMetadata(), value.Copy());
      return;
    }
    if (key == "x-envoy-peer") {
      Set(XEnvoyPeerMetadata(), value.Copy());
      return;
    }
    if (key == "x-forwarded-for") {
      Set(XForwardedForMetadata(), value.Copy());
      return;
    }
    if (key == "x-forwarded-host") {
      Set(XForwardedHostMetadata(), value.Copy());
      return;
    }
    if (key == "grpc-retry-pushback-ms") {
      auto memento = GrpcRetryPushbackMsMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcRetryPushbackMsMetadata(), GrpcRetryPushbackMsMetadata::MementoToValue(memento));
      return;
    }
    if (key == "grpc-timeout") {
      auto memento = GrpcTimeoutMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcTimeoutMetadata(), GrpcTimeoutMetadata::MementoToValue(memento));
      return;
    }
    if (key == "grpc-lb-client-stats") {
      auto memento = GrpcLbClientStatsMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcLbClientStatsMetadata(), GrpcLbClientStatsMetadata::MementoToValue(memento));
      return;
    }
    if (key == "content-type") {
      auto memento = ContentTypeMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(ContentTypeMetadata(), ContentTypeMetadata::MementoToValue(memento));
      return;
    }
    if (key == "grpc-accept-encoding") {
      auto memento = GrpcAcceptEncodingMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcAcceptEncodingMetadata(), GrpcAcceptEncodingMetadata::MementoToValue(memento));
      return;
    }
    if (key == "grpc-encoding") {
      auto memento = GrpcEncodingMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcEncodingMetadata(), GrpcEncodingMetadata::MementoToValue(memento));
      return;
    }
    if (key == "grpc-internal-encoding-request") {
      auto memento = GrpcInternalEncodingRequest::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcInternalEncodingRequest(), GrpcInternalEncodingRequest::MementoToValue(memento));
      return;
    }
    if (key == "grpc-previous-rpc-attempts") {
      auto memento = GrpcPreviousRpcAttemptsMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcPreviousRpcAttemptsMetadata(), GrpcPreviousRpcAttemptsMetadata::MementoToValue(memento));
      return;
    }
    if (key == "grpc-status") {
      auto memento = GrpcStatusMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(GrpcStatusMetadata(), GrpcStatusMetadata::MementoToValue(memento));
      return;
    }
    if (key == ":method") {
      auto memento = HttpMethodMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(HttpMethodMetadata(), HttpMethodMetadata::MementoToValue(memento));
      return;
    }
    if (key == ":scheme") {
      auto memento = HttpSchemeMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(HttpSchemeMetadata(), HttpSchemeMetadata::MementoToValue(memento));
      return;
    }
    if (key == ":status") {
      auto memento = HttpStatusMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(HttpStatusMetadata(), HttpStatusMetadata::MementoToValue(memento));
      return;
    }
    if (key == "lb-cost-bin") {
      auto memento = LbCostBinMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(LbCostBinMetadata(), LbCostBinMetadata::MementoToValue(memento));
      return;
    }
    if (key == "te") {
      auto memento = TeMetadata::ParseMemento(value.Copy(), false, on_error);
      Set(TeMetadata(), TeMetadata::MementoToValue(memento));
      return;
    }
    unknown_.Append(key, std::move(value));
  }

  template <template <typename, typename> class Factory> struct StatefulCompressor
    : public Factory<EndpointLoadMetricsBinMetadata, typename EndpointLoadMetricsBinMetadata::CompressionTraits>
    , public Factory<GrpcMessageMetadata, typename GrpcMessageMetadata::CompressionTraits>
    , public Factory<GrpcServerStatsBinMetadata, typename GrpcServerStatsBinMetadata::CompressionTraits>
    , public Factory<GrpcTagsBinMetadata, typename GrpcTagsBinMetadata::CompressionTraits>
    , public Factory<GrpcTraceBinMetadata, typename GrpcTraceBinMetadata::CompressionTraits>
    , public Factory<HostMetadata, typename HostMetadata::CompressionTraits>
    , public Factory<HttpAuthorityMetadata, typename HttpAuthorityMetadata::CompressionTraits>
    , public Factory<HttpPathMetadata, typename HttpPathMetadata::CompressionTraits>
    , public Factory<LbTokenMetadata, typename LbTokenMetadata::CompressionTraits>
    , public Factory<UserAgentMetadata, typename UserAgentMetadata::CompressionTraits>
    , public Factory<W3CTraceParentMetadata, typename W3CTraceParentMetadata::CompressionTraits>
    , public Factory<XEnvoyPeerMetadata, typename XEnvoyPeerMetadata::CompressionTraits>
    , public Factory<XForwardedForMetadata, typename XForwardedForMetadata::CompressionTraits>
    , public Factory<XForwardedHostMetadata, typename XForwardedHostMetadata::CompressionTraits>
    , public Factory<GrpcRetryPushbackMsMetadata, typename GrpcRetryPushbackMsMetadata::CompressionTraits>
    , public Factory<GrpcTimeoutMetadata, typename GrpcTimeoutMetadata::CompressionTraits>
    , public Factory<GrpcLbClientStatsMetadata, typename GrpcLbClientStatsMetadata::CompressionTraits>
    , public Factory<ContentTypeMetadata, typename ContentTypeMetadata::CompressionTraits>
    , public Factory<GrpcAcceptEncodingMetadata, typename GrpcAcceptEncodingMetadata::CompressionTraits>
    , public Factory<GrpcEncodingMetadata, typename GrpcEncodingMetadata::CompressionTraits>
    , public Factory<GrpcInternalEncodingRequest, typename GrpcInternalEncodingRequest::CompressionTraits>
    , public Factory<GrpcPreviousRpcAttemptsMetadata, typename GrpcPreviousRpcAttemptsMetadata::CompressionTraits>
    , public Factory<GrpcStatusMetadata, typename GrpcStatusMetadata::CompressionTraits>
    , public Factory<HttpMethodMetadata, typename HttpMethodMetadata::CompressionTraits>
    , public Factory<HttpSchemeMetadata, typename HttpSchemeMetadata::CompressionTraits>
    , public Factory<HttpStatusMetadata, typename HttpStatusMetadata::CompressionTraits>
    , public Factory<LbCostBinMetadata, typename LbCostBinMetadata::CompressionTraits>
    , public Factory<TeMetadata, typename TeMetadata::CompressionTraits>
  {};

  void Log(absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn) const {}
  std::string DebugString() const { return ""; }
  bool empty() const { return count() == 0; }
};
bool IsMetadataKeyAllowedInDebugOutput(absl::string_view key);

}  // namespace grpc_core
using grpc_metadata_batch = grpc_core::grpc_metadata_batch;
#endif  // GRPC_SRC_CORE_CALL_METADATA_BATCH_H

