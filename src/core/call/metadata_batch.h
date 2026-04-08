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

struct GrpcLbClientStats;

using MetadataParseErrorFn = absl::FunctionRef<void(absl::string_view, const Slice&)>;


struct GrpcTarPit {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = Empty;
  using MementoType = Empty;
  static absl::string_view DebugKey() { return "GrpcTarPit"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view DebugKey() { return "GrpcCallWasCancelled"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view DebugKey() { return "GrpcStatusFromWire"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view DebugKey() { return "GrpcTrailersOnly"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view DebugKey() { return "IsTransparentRetry"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "EndpointLoadMetricsBinMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcMessageMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcServerStatsBinMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcTagsBinMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcTraceBinMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "HostMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "HttpAuthorityMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "HttpPathMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "LbTokenMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view DebugKey() { return "PeerString"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "UserAgentMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "W3CTraceParentMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "XEnvoyPeerMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "XForwardedForMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "XForwardedHostMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view DebugKey() { return "GrpcStatusContext"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcRetryPushbackMsMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
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
  static absl::string_view key() { return "GrpcTimeoutMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcLbClientStatsMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
on_error("not a valid value for grpclb_client_stats", Slice());
    return nullptr;
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
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
  static absl::string_view DebugKey() { return "GrpcRegisteredMethod"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "ContentTypeMetadata"; }
  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcAcceptEncodingMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
return CompressionAlgorithmSet::FromString(value.as_string_view());
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
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
  static absl::string_view key() { return "GrpcEncodingMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcInternalEncodingRequest"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcPreviousRpcAttemptsMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "GrpcStatusMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
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
  static absl::string_view DebugKey() { return "GrpcStreamNetworkState"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HttpMethodMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  static absl::string_view key() { return "HttpMethodMetadata"; }
  enum class ValueType : uint32_t { kPost, kGet, kPut, kInvalid };
  using MementoType = ValueType;
  static constexpr auto kPost = ValueType::kPost;
  static constexpr auto kGet = ValueType::kGet;
  static constexpr auto kPut = ValueType::kPut;
  static constexpr auto kInvalid = ValueType::kInvalid;
  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);
  static StaticSlice Encode(ValueType x);
  static const char* DisplayValue(ValueType x);
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HttpSchemeMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  static absl::string_view key() { return "HttpSchemeMetadata"; }
  enum class ValueType : uint32_t { kHttp, kHttps, kInvalid };
  using MementoType = ValueType;
  static constexpr auto kHttp = ValueType::kHttp;
  static constexpr auto kHttps = ValueType::kHttps;
  static constexpr auto kInvalid = ValueType::kInvalid;
  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);
  static StaticSlice Encode(ValueType x);
  static const char* DisplayValue(ValueType x);
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
return Parse(value.as_string_view(), on_error);
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
  }
  static auto DisplayValue(const ValueType& x) {
return "<unsupported>";
  }
};
struct HttpStatusMetadata {
  static constexpr bool kPublishToApp = false;
  static constexpr bool kRepeatable = false;
  static constexpr bool kTransferOnTrailersOnly = false;
  using ValueType = uint32_t;
  using MementoType = uint32_t;
  static absl::string_view key() { return "HttpStatusMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
  static absl::string_view key() { return "LbCostBinMetadata"; }
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
  static auto Encode(const ValueType& x) {
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
  static absl::string_view key() { return "TeMetadata"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
if (value == StaticSlice::FromStaticString("trailers")) return kTrailers; return kInvalid;
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
GRPC_CHECK(x == kTrailers);
    return StaticSlice::FromStaticString("trailers");
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
  static absl::string_view DebugKey() { return "WaitForReady"; }
  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {
abort();
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static auto Encode(const ValueType& x) {
abort();
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
MetadataValueAsSlice(typename Which::ValueType value) { return Slice(); }

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
  void Log(absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn) const {}
  std::string DebugString() const { return ""; }
  bool empty() const { return count() == 0; }
  template <typename Trait, typename Value> void Set(Trait, Value) {}
  void Set(const ParsedMetadata<grpc_metadata_batch>&) {}
  size_t TransportSize() const { return 0; }
  template <typename Trait> const typename Trait::ValueType* get_pointer(Trait) const { return nullptr; }
  struct DummyValue { int value = 0; bool explicitly_set = false; void emplace_back(const std::string&) {} };
  template <typename Trait> DummyValue* GetOrCreatePointer(Trait) { static DummyValue dummy; return &dummy; }
  template <typename Trait> typename Trait::ValueType* get_pointer(Trait) { return nullptr; }
  const std::vector<std::string>* get_pointer(GrpcStatusContext) const { return nullptr; }
  template <typename Encoder> void Encode(Encoder*) const {}
  static ParsedMetadata<grpc_metadata_batch> Parse(absl::string_view, Slice, bool, uint32_t, MetadataParseErrorFn) { return ParsedMetadata<grpc_metadata_batch>(); }
  grpc_metadata_batch Copy() const { return grpc_metadata_batch(); }
  std::optional<absl::string_view> GetStringValue(absl::string_view key, std::string* buffer) const { return unknown_.GetStringValue(key, buffer); }
  void Remove(absl::string_view key) { unknown_.Remove(key); }
  void Clear() { unknown_.Clear(); }
  template <typename Trait> std::optional<typename Trait::ValueType> Take(Trait) { return std::nullopt; }
  template <typename Trait> void Remove(Trait) {}
  size_t count() const {
    size_t c = 0;
    for (auto b : flags_) c += absl::popcount(b);
    return c + unknown_.size();
  }
  void Append(absl::string_view key, Slice value, MetadataParseErrorFn on_error) {
    unknown_.Append(key, std::move(value));
  }
  template <template <typename, typename> class Factory> struct StatefulCompressor {};
};
bool IsMetadataKeyAllowedInDebugOutput(absl::string_view key);
}  // namespace grpc_core
using grpc_metadata_batch = grpc_core::grpc_metadata_batch;
#endif  // GRPC_SRC_CORE_CALL_METADATA_BATCH_H
