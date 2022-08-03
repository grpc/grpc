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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/metadata_batch.h"

#include <string.h>

#include <algorithm>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {
namespace metadata_detail {

void DebugStringBuilder::Add(absl::string_view key, absl::string_view value) {
  if (!out_.empty()) out_.append(", ");
  absl::StrAppend(&out_, absl::CEscape(key), ": ", absl::CEscape(value));
}

void UnknownMap::Append(absl::string_view key, Slice value) {
  unknown_.EmplaceBack(Slice::FromCopiedString(key), value.Ref());
}

void UnknownMap::Remove(absl::string_view key) {
  unknown_.SetEnd(std::remove_if(unknown_.begin(), unknown_.end(),
                                 [key](const std::pair<Slice, Slice>& p) {
                                   return p.first.as_string_view() == key;
                                 }));
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
    Slice value, MetadataParseErrorFn on_error) {
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
    on_error("invalid value", value);
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

const char* ContentTypeMetadata::DisplayValue(MementoType content_type) {
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
    Slice value, MetadataParseErrorFn on_error) {
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
  return ExecCtx::Get()->Now() + timeout;
}

Slice GrpcTimeoutMetadata::Encode(ValueType x) {
  return Timeout::FromDuration(x - ExecCtx::Get()->Now()).Encode();
}

TeMetadata::MementoType TeMetadata::ParseMemento(
    Slice value, MetadataParseErrorFn on_error) {
  auto out = kInvalid;
  if (value == "trailers") {
    out = kTrailers;
  } else {
    on_error("invalid value", value);
  }
  return out;
}

const char* TeMetadata::DisplayValue(MementoType te) {
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

const char* HttpSchemeMetadata::DisplayValue(MementoType content_type) {
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
    Slice value, MetadataParseErrorFn on_error) {
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
      abort();
  }
}

const char* HttpMethodMetadata::DisplayValue(MementoType content_type) {
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
CompressionAlgorithmBasedMetadata::ParseMemento(Slice value,
                                                MetadataParseErrorFn on_error) {
  auto algorithm = ParseCompressionAlgorithm(value.as_string_view());
  if (!algorithm.has_value()) {
    on_error("invalid value", value);
    return GRPC_COMPRESS_NONE;
  }
  return *algorithm;
}

Duration GrpcRetryPushbackMsMetadata::ParseMemento(
    Slice value, MetadataParseErrorFn on_error) {
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

std::string LbCostBinMetadata::DisplayValue(MementoType x) {
  return absl::StrCat(x.name, ":", x.cost);
}

LbCostBinMetadata::MementoType LbCostBinMetadata::ParseMemento(
    Slice value, MetadataParseErrorFn on_error) {
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

std::string PeerString::DisplayValue(ValueType x) { return std::string(x); }
const std::string& GrpcStatusContext::DisplayValue(const std::string& x) {
  return x;
}

std::string WaitForReady::DisplayValue(ValueType x) {
  return absl::StrCat(x.value ? "true" : "false",
                      x.explicitly_set ? " (explicit)" : "");
}

}  // namespace grpc_core
