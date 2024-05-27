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

#include "src/core/lib/transport/metadata_batch.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {
namespace metadata_detail {

void DebugStringBuilder::Add(absl::string_view key, absl::string_view value) {
  if (!out_.empty()) out_.append(", ");
  absl::StrAppend(&out_, absl::CEscape(key), ": ", absl::CEscape(value));
}

void DebugStringBuilder::AddAfterRedaction(absl::string_view key,
                                           absl::string_view value) {
  if (IsAllowListed(key)) {
    Add(key, value);
  } else {
    Add(key, absl::StrCat(value.size(), " bytes redacted by allow listing."));
  }
}

bool DebugStringBuilder::IsAllowListed(const absl::string_view key) const {
  static const absl::NoDestructor<absl::flat_hash_set<std::string>> allow_list(
      [] {
        absl::flat_hash_set<std::string> allow_list;
        // go/keep-sorted start
        allow_list.insert(std::string(ContentTypeMetadata::key()));
        allow_list.insert(std::string(EndpointLoadMetricsBinMetadata::key()));
        allow_list.insert(std::string(GrpcAcceptEncodingMetadata::key()));
        allow_list.insert(std::string(GrpcEncodingMetadata::key()));
        allow_list.insert(std::string(GrpcInternalEncodingRequest::key()));
        allow_list.insert(std::string(GrpcLbClientStatsMetadata::key()));
        allow_list.insert(std::string(GrpcMessageMetadata::key()));
        allow_list.insert(std::string(GrpcPreviousRpcAttemptsMetadata::key()));
        allow_list.insert(std::string(GrpcRetryPushbackMsMetadata::key()));
        allow_list.insert(std::string(GrpcServerStatsBinMetadata::key()));
        allow_list.insert(std::string(GrpcStatusMetadata::key()));
        allow_list.insert(std::string(GrpcTagsBinMetadata::key()));
        allow_list.insert(std::string(GrpcTimeoutMetadata::key()));
        allow_list.insert(std::string(GrpcTraceBinMetadata::key()));
        allow_list.insert(std::string(HostMetadata::key()));
        allow_list.insert(std::string(HttpAuthorityMetadata::key()));
        allow_list.insert(std::string(HttpMethodMetadata::key()));
        allow_list.insert(std::string(HttpPathMetadata::key()));
        allow_list.insert(std::string(HttpSchemeMetadata::key()));
        allow_list.insert(std::string(HttpStatusMetadata::key()));
        allow_list.insert(std::string(LbCostBinMetadata::key()));
        allow_list.insert(std::string(LbTokenMetadata::key()));
        allow_list.insert(std::string(TeMetadata::key()));
        allow_list.insert(std::string(UserAgentMetadata::key()));
        allow_list.insert(std::string(XEnvoyPeerMetadata::key()));

        // go/keep-sorted end
        // go/keep-sorted start
        allow_list.insert(std::string(GrpcCallWasCancelled::DebugKey()));
        allow_list.insert(std::string(GrpcRegisteredMethod::DebugKey()));
        allow_list.insert(std::string(GrpcStatusContext::DebugKey()));
        allow_list.insert(std::string(GrpcStatusFromWire::DebugKey()));
        allow_list.insert(std::string(GrpcStreamNetworkState::DebugKey()));
        allow_list.insert(std::string(GrpcTarPit::DebugKey()));
        allow_list.insert(std::string(GrpcTrailersOnly::DebugKey()));
        allow_list.insert(std::string(PeerString::DebugKey()));
        allow_list.insert(std::string(WaitForReady::DebugKey()));
        // go/keep-sorted end
        return allow_list;
      }());
  return allow_list->contains(key);
}

void UnknownMap::Append(absl::string_view key, Slice value) {
  unknown_.emplace_back(Slice::FromCopiedString(key), value.Ref());
}

void UnknownMap::Remove(absl::string_view key) {
  unknown_.erase(std::remove_if(unknown_.begin(), unknown_.end(),
                                [key](const std::pair<Slice, Slice>& p) {
                                  return p.first.as_string_view() == key;
                                }),
                 unknown_.end());
}

absl::optional<absl::string_view> UnknownMap::GetStringValue(
    absl::string_view key, std::string* backing) const {
  absl::optional<absl::string_view> out;
  for (const auto& p : unknown_) {
    if (p.first.as_string_view() == key) {
      if (!out.has_value()) {
        out = p.second.as_string_view();
      } else {
        out = *backing = absl::StrCat(*out, ",", p.second.as_string_view());
      }
    }
  }
  return out;
}

}  // namespace metadata_detail

ContentTypeMetadata::MementoType ContentTypeMetadata::ParseMemento(
    Slice value, bool, MetadataParseErrorFn /*on_error*/) {
  auto out = kInvalid;
  auto value_string = value.as_string_view();
  if (value_string == "application/grpc") {
    out = kApplicationGrpc;
  } else if (absl::StartsWith(value_string, "application/grpc;")) {
    out = kApplicationGrpc;
  } else if (absl::StartsWith(value_string, "application/grpc+")) {
    out = kApplicationGrpc;
  } else if (value_string.empty()) {
    out = kEmpty;
  } else {
    // We are intentionally not invoking on_error here since the spec is not
    // clear on what the behavior should be here, so to avoid breaking anyone,
    // we should continue to accept this.
  }
  return out;
}

StaticSlice ContentTypeMetadata::Encode(ValueType x) {
  switch (x) {
    case kEmpty:
      return StaticSlice::FromStaticString("");
    case kApplicationGrpc:
      return StaticSlice::FromStaticString("application/grpc");
    case kInvalid:
      return StaticSlice::FromStaticString("application/grpc+unknown");
  }
  GPR_UNREACHABLE_CODE(
      return StaticSlice::FromStaticString("unrepresentable value"));
}

const char* ContentTypeMetadata::DisplayValue(ValueType content_type) {
  switch (content_type) {
    case ValueType::kApplicationGrpc:
      return "application/grpc";
    case ValueType::kEmpty:
      return "";
    default:
      return "<discarded-invalid-value>";
  }
}

GrpcTimeoutMetadata::MementoType GrpcTimeoutMetadata::ParseMemento(
    Slice value, bool, MetadataParseErrorFn on_error) {
  auto timeout = ParseTimeout(value);
  if (!timeout.has_value()) {
    on_error("invalid value", value);
    return Duration::Infinity();
  }
  return *timeout;
}

GrpcTimeoutMetadata::ValueType GrpcTimeoutMetadata::MementoToValue(
    MementoType timeout) {
  if (timeout == Duration::Infinity()) {
    return Timestamp::InfFuture();
  }
  return Timestamp::Now() + timeout;
}

Slice GrpcTimeoutMetadata::Encode(ValueType x) {
  return Timeout::FromDuration(x - Timestamp::Now()).Encode();
}

TeMetadata::MementoType TeMetadata::ParseMemento(
    Slice value, bool, MetadataParseErrorFn on_error) {
  auto out = kInvalid;
  if (value == "trailers") {
    out = kTrailers;
  } else {
    on_error("invalid value", value);
  }
  return out;
}

const char* TeMetadata::DisplayValue(ValueType te) {
  switch (te) {
    case ValueType::kTrailers:
      return "trailers";
    default:
      return "<discarded-invalid-value>";
  }
}

HttpSchemeMetadata::ValueType HttpSchemeMetadata::Parse(
    absl::string_view value, MetadataParseErrorFn on_error) {
  if (value == "http") {
    return kHttp;
  } else if (value == "https") {
    return kHttps;
  }
  on_error("invalid value", Slice::FromCopiedBuffer(value));
  return kInvalid;
}

StaticSlice HttpSchemeMetadata::Encode(ValueType x) {
  switch (x) {
    case kHttp:
      return StaticSlice::FromStaticString("http");
    case kHttps:
      return StaticSlice::FromStaticString("https");
    default:
      abort();
  }
}

size_t EncodedSizeOfKey(HttpSchemeMetadata, HttpSchemeMetadata::ValueType x) {
  switch (x) {
    case HttpSchemeMetadata::kHttp:
      return 4;
    case HttpSchemeMetadata::kHttps:
      return 5;
    default:
      return 0;
  }
}

const char* HttpSchemeMetadata::DisplayValue(ValueType content_type) {
  switch (content_type) {
    case kHttp:
      return "http";
    case kHttps:
      return "https";
    default:
      return "<discarded-invalid-value>";
  }
}

HttpMethodMetadata::MementoType HttpMethodMetadata::ParseMemento(
    Slice value, bool, MetadataParseErrorFn on_error) {
  auto out = kInvalid;
  auto value_string = value.as_string_view();
  if (value_string == "POST") {
    out = kPost;
  } else if (value_string == "PUT") {
    out = kPut;
  } else if (value_string == "GET") {
    out = kGet;
  } else {
    on_error("invalid value", value);
  }
  return out;
}

StaticSlice HttpMethodMetadata::Encode(ValueType x) {
  switch (x) {
    case kPost:
      return StaticSlice::FromStaticString("POST");
    case kPut:
      return StaticSlice::FromStaticString("PUT");
    case kGet:
      return StaticSlice::FromStaticString("GET");
    default:
      // TODO(ctiller): this should be an abort, we should split up the debug
      // string generation from the encode string generation so that debug
      // strings can always succeed and encode strings can crash.
      return StaticSlice::FromStaticString("<<INVALID METHOD>>");
  }
}

const char* HttpMethodMetadata::DisplayValue(ValueType content_type) {
  switch (content_type) {
    case kPost:
      return "POST";
    case kGet:
      return "GET";
    case kPut:
      return "PUT";
    default:
      return "<discarded-invalid-value>";
  }
}

CompressionAlgorithmBasedMetadata::MementoType
CompressionAlgorithmBasedMetadata::ParseMemento(Slice value, bool,
                                                MetadataParseErrorFn on_error) {
  auto algorithm = ParseCompressionAlgorithm(value.as_string_view());
  if (!algorithm.has_value()) {
    on_error("invalid value", value);
    return GRPC_COMPRESS_NONE;
  }
  return *algorithm;
}

Duration GrpcRetryPushbackMsMetadata::ParseMemento(
    Slice value, bool, MetadataParseErrorFn on_error) {
  int64_t out;
  if (!absl::SimpleAtoi(value.as_string_view(), &out)) {
    on_error("not an integer", value);
    return Duration::NegativeInfinity();
  }
  return Duration::Milliseconds(out);
}

Slice LbCostBinMetadata::Encode(const ValueType& x) {
  auto slice =
      MutableSlice::CreateUninitialized(sizeof(double) + x.name.length());
  memcpy(slice.data(), &x.cost, sizeof(double));
  memcpy(slice.data() + sizeof(double), x.name.data(), x.name.length());
  return Slice(std::move(slice));
}

std::string LbCostBinMetadata::DisplayValue(ValueType x) {
  return absl::StrCat(x.name, ":", x.cost);
}

LbCostBinMetadata::MementoType LbCostBinMetadata::ParseMemento(
    Slice value, bool, MetadataParseErrorFn on_error) {
  if (value.length() < sizeof(double)) {
    on_error("too short", value);
    return {0, ""};
  }
  MementoType out;
  memcpy(&out.cost, value.data(), sizeof(double));
  out.name =
      std::string(reinterpret_cast<const char*>(value.data()) + sizeof(double),
                  value.length() - sizeof(double));
  return out;
}

std::string GrpcStreamNetworkState::DisplayValue(ValueType x) {
  switch (x) {
    case kNotSentOnWire:
      return "not sent on wire";
    case kNotSeenByServer:
      return "not seen by server";
  }
  GPR_UNREACHABLE_CODE(return "unknown value");
}

std::string GrpcRegisteredMethod::DisplayValue(void* x) {
  return absl::StrFormat("%p", x);
}

std::string PeerString::DisplayValue(const ValueType& x) {
  return std::string(x.as_string_view());
}

const std::string& GrpcStatusContext::DisplayValue(const std::string& x) {
  return x;
}

std::string WaitForReady::DisplayValue(ValueType x) {
  return absl::StrCat(x.value ? "true" : "false",
                      x.explicitly_set ? " (explicit)" : "");
}

}  // namespace grpc_core
