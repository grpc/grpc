/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H
#define GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include <limits>

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/time.h>

#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/gprpp/chunked_vector.h"
#include "src/core/lib/gprpp/table.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/parsed_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {

// grpc-timeout metadata trait.
// ValueType is defined as grpc_millis - an absolute timestamp (i.e. a
// deadline!), that is converted to a duration by transports before being
// sent.
// TODO(ctiller): Move this elsewhere. During the transition we need to be able
// to name this in MetadataMap, but ultimately once the transition is done we
// should not need to.
struct GrpcTimeoutMetadata {
  static constexpr bool kRepeatable = false;
  using ValueType = grpc_millis;
  using MementoType = grpc_millis;
  static absl::string_view key() { return "grpc-timeout"; }
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
    auto timeout = ParseTimeout(value);
    if (!timeout.has_value()) {
      on_error("invalid value", value);
      return GRPC_MILLIS_INF_FUTURE;
    }
    return *timeout;
  }
  static ValueType MementoToValue(MementoType timeout) {
    if (timeout == GRPC_MILLIS_INF_FUTURE) {
      return GRPC_MILLIS_INF_FUTURE;
    }
    return ExecCtx::Get()->Now() + timeout;
  }
  static Slice Encode(ValueType x) {
    return Timeout::FromDuration(x - ExecCtx::Get()->Now()).Encode();
  }
  static MementoType DisplayValue(MementoType x) { return x; }
};

// TE metadata trait.
struct TeMetadata {
  static constexpr bool kRepeatable = false;
  // HTTP2 says that TE can either be empty or "trailers".
  // Empty means this trait is not included, "trailers" means kTrailers, and
  // kInvalid is used to remember an invalid value.
  enum ValueType : uint8_t {
    kTrailers,
    kInvalid,
  };
  using MementoType = ValueType;
  static absl::string_view key() { return "te"; }
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
    auto out = kInvalid;
    if (value == "trailers") {
      out = kTrailers;
    } else {
      on_error("invalid value", value);
    }
    return out;
  }
  static ValueType MementoToValue(MementoType te) { return te; }
  static StaticSlice Encode(ValueType x) {
    GPR_ASSERT(x == kTrailers);
    return StaticSlice::FromStaticString("trailers");
  }
  static const char* DisplayValue(MementoType te) {
    switch (te) {
      case ValueType::kTrailers:
        return "trailers";
      default:
        return "<discarded-invalid-value>";
    }
  }
};

// content-type metadata trait.
struct ContentTypeMetadata {
  static constexpr bool kRepeatable = false;
  // gRPC says that content-type can be application/grpc[;something]
  // Core has only ever verified the prefix.
  // IF we want to start verifying more, we can expand this type.
  enum ValueType {
    kApplicationGrpc,
    kEmpty,
    kInvalid,
  };
  using MementoType = ValueType;
  static absl::string_view key() { return "content-type"; }
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
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
  static ValueType MementoToValue(MementoType content_type) {
    return content_type;
  }
  static StaticSlice Encode(ValueType x) {
    switch (x) {
      case kEmpty:
        return StaticSlice::FromStaticString("");
      case kApplicationGrpc:
        return StaticSlice::FromStaticString("application/grpc");
      case kInvalid:
        abort();
    }
    GPR_UNREACHABLE_CODE(
        return StaticSlice::FromStaticString("unrepresentable value"));
  }
  static const char* DisplayValue(MementoType content_type) {
    switch (content_type) {
      case ValueType::kApplicationGrpc:
        return "application/grpc";
      case ValueType::kEmpty:
        return "";
      default:
        return "<discarded-invalid-value>";
    }
  }
};

// scheme metadata trait.
struct HttpSchemeMetadata {
  static constexpr bool kRepeatable = false;
  enum ValueType {
    kHttp,
    kHttps,
    kInvalid,
  };
  using MementoType = ValueType;
  static absl::string_view key() { return ":scheme"; }
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
    return Parse(value.as_string_view(), on_error);
  }
  static ValueType Parse(absl::string_view value,
                         MetadataParseErrorFn on_error) {
    if (value == "http") {
      return kHttp;
    } else if (value == "https") {
      return kHttps;
    }
    on_error("invalid value", Slice::FromCopiedBuffer(value));
    return kInvalid;
  }
  static ValueType MementoToValue(MementoType content_type) {
    return content_type;
  }
  static StaticSlice Encode(ValueType x) {
    switch (x) {
      case kHttp:
        return StaticSlice::FromStaticString("http");
      case kHttps:
        return StaticSlice::FromStaticString("https");
      default:
        abort();
    }
  }
  static const char* DisplayValue(MementoType content_type) {
    switch (content_type) {
      case kHttp:
        return "http";
      case kHttps:
        return "https";
      default:
        return "<discarded-invalid-value>";
    }
  }
};

// method metadata trait.
struct HttpMethodMetadata {
  static constexpr bool kRepeatable = false;
  enum ValueType {
    kPost,
    kPut,
    kGet,
    kInvalid,
  };
  using MementoType = ValueType;
  static absl::string_view key() { return ":method"; }
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
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
  static ValueType MementoToValue(MementoType content_type) {
    return content_type;
  }
  static StaticSlice Encode(ValueType x) {
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
  static const char* DisplayValue(MementoType content_type) {
    switch (content_type) {
      case kPost:
        return "POST";
      case kPut:
        return "PUT";
      case kGet:
        return "GET";
      default:
        return "<discarded-invalid-value>";
    }
  }
};

// Base type for metadata pertaining to a single compression algorithm
// (e.g., "grpc-encoding").
struct CompressionAlgorithmBasedMetadata {
  using ValueType = grpc_compression_algorithm;
  using MementoType = ValueType;
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
    auto algorithm = ParseCompressionAlgorithm(value.as_string_view());
    if (!algorithm.has_value()) {
      on_error("invalid value", value);
      return GRPC_COMPRESS_NONE;
    }
    return *algorithm;
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(ValueType x) {
    GPR_ASSERT(x != GRPC_COMPRESS_ALGORITHMS_COUNT);
    return Slice::FromStaticString(CompressionAlgorithmAsString(x));
  }
  static const char* DisplayValue(MementoType x) {
    if (const char* p = CompressionAlgorithmAsString(x)) {
      return p;
    } else {
      return "<discarded-invalid-value>";
    }
  }
};

// grpc-encoding metadata trait.
struct GrpcEncodingMetadata : public CompressionAlgorithmBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-encoding"; }
};

// grpc-internal-encoding-request metadata trait.
struct GrpcInternalEncodingRequest : public CompressionAlgorithmBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-internal-encoding-request"; }
};

// grpc-accept-encoding metadata trait.
struct GrpcAcceptEncodingMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-accept-encoding"; }
  using ValueType = CompressionAlgorithmSet;
  using MementoType = ValueType;
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn) {
    return CompressionAlgorithmSet::FromString(value.as_string_view());
  }
  static ValueType MementoToValue(MementoType x) { return x; }
  static Slice Encode(ValueType x) { return x.ToSlice(); }
  static std::string DisplayValue(MementoType x) { return x.ToString(); }
};

struct SimpleSliceBasedMetadata {
  using ValueType = Slice;
  using MementoType = Slice;
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn) {
    return value.TakeOwned();
  }
  static ValueType MementoToValue(MementoType value) { return value; }
  static Slice Encode(const ValueType& x) { return x.Ref(); }
  static absl::string_view DisplayValue(const MementoType& value) {
    return value.as_string_view();
  }
};

// user-agent metadata trait.
struct UserAgentMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "user-agent"; }
};

// grpc-message metadata trait.
struct GrpcMessageMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-message"; }
};

// host metadata trait.
struct HostMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "host"; }
};

// x-endpoint-load-metrics-bin metadata trait.
struct XEndpointLoadMetricsBinMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "x-endpoint-load-metrics-bin"; }
};

// grpc-server-stats-bin metadata trait.
struct GrpcServerStatsBinMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-server-stats-bin"; }
};

// grpc-trace-bin metadata trait.
struct GrpcTraceBinMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-trace-bin"; }
};

// grpc-tags-bin metadata trait.
struct GrpcTagsBinMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-tags-bin"; }
};

// :authority metadata trait.
struct HttpAuthorityMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return ":authority"; }
};

// :path metadata trait.
struct HttpPathMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return ":path"; }
};

// We separate SimpleIntBasedMetadata into two pieces: one that does not depend
// on the invalid value, and one that does. This allows the compiler to easily
// see the functions that are shared, and helps reduce code bloat here.
template <typename Int>
struct SimpleIntBasedMetadataBase {
  using ValueType = Int;
  using MementoType = Int;
  static ValueType MementoToValue(MementoType value) { return value; }
  static Slice Encode(ValueType x) { return Slice::FromInt64(x); }
  static Int DisplayValue(MementoType x) { return x; }
};

template <typename Int, Int kInvalidValue>
struct SimpleIntBasedMetadata : public SimpleIntBasedMetadataBase<Int> {
  static constexpr Int invalid_value() { return kInvalidValue; }
  static Int ParseMemento(Slice value, MetadataParseErrorFn on_error) {
    Int out;
    if (!absl::SimpleAtoi(value.as_string_view(), &out)) {
      on_error("not an integer", value);
      out = kInvalidValue;
    }
    return out;
  }
};

// grpc-status metadata trait.
struct GrpcStatusMetadata
    : public SimpleIntBasedMetadata<grpc_status_code, GRPC_STATUS_UNKNOWN> {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-status"; }
};

// grpc-previous-rpc-attempts metadata trait.
struct GrpcPreviousRpcAttemptsMetadata
    : public SimpleIntBasedMetadata<uint32_t, 0> {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-previous-rpc-attempts"; }
};

// grpc-retry-pushback-ms metadata trait.
struct GrpcRetryPushbackMsMetadata
    : public SimpleIntBasedMetadata<grpc_millis, GRPC_MILLIS_INF_PAST> {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpc-retry-pushback-ms"; }
};

// :status metadata trait.
// TODO(ctiller): consider moving to uint16_t
struct HttpStatusMetadata : public SimpleIntBasedMetadata<uint32_t, 0> {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return ":status"; }
};

// "secret" metadata trait used to pass load balancing token between filters.
// This should not be exposed outside of gRPC core.
class GrpcLbClientStats;
struct GrpcLbClientStatsMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "grpclb_client_stats"; }
  using ValueType = GrpcLbClientStats*;
  using MementoType = ValueType;
  static ValueType MementoToValue(MementoType value) { return value; }
  static Slice Encode(ValueType) { abort(); }
  static const char* DisplayValue(MementoType) { return "<internal-lb-stats>"; }
  static MementoType ParseMemento(Slice, MetadataParseErrorFn) {
    return nullptr;
  }
};

// lb-token metadata
struct LbTokenMetadata : public SimpleSliceBasedMetadata {
  static constexpr bool kRepeatable = false;
  static absl::string_view key() { return "lb-token"; }
};

// lb-cost-bin metadata
struct LbCostBinMetadata {
  static constexpr bool kRepeatable = true;
  static absl::string_view key() { return "lb-cost-bin"; }
  struct ValueType {
    double cost;
    std::string name;
  };
  using MementoType = ValueType;
  static ValueType MementoToValue(MementoType value) { return value; }
  static Slice Encode(const ValueType& x) {
    auto slice =
        MutableSlice::CreateUninitialized(sizeof(double) + x.name.length());
    memcpy(slice.data(), &x.cost, sizeof(double));
    memcpy(slice.data() + sizeof(double), x.name.data(), x.name.length());
    return Slice(std::move(slice));
  }
  static std::string DisplayValue(MementoType x) {
    return absl::StrCat(x.name, ":", x.cost);
  }
  static MementoType ParseMemento(Slice value, MetadataParseErrorFn on_error) {
    if (value.length() < sizeof(double)) {
      on_error("too short", value);
      return {0, ""};
    }
    MementoType out;
    memcpy(&out.cost, value.data(), sizeof(double));
    out.name = std::string(
        reinterpret_cast<const char*>(value.data()) + sizeof(double),
        value.length() - sizeof(double));
    return out;
  }
};

namespace metadata_detail {

// Helper type - maps a string name to a trait.
template <typename... Traits>
struct NameLookup;

template <typename Trait, typename... Traits>
struct NameLookup<Trait, Traits...> {
  // Call op->Found(Trait()) if op->name == Trait::key() for some Trait in
  // Traits. If not found, call op->NotFound().
  template <typename Op>
  static auto Lookup(absl::string_view key, Op* op)
      -> decltype(op->Found(Trait())) {
    if (key == Trait::key()) {
      return op->Found(Trait());
    }
    return NameLookup<Traits...>::Lookup(key, op);
  }
};

template <>
struct NameLookup<> {
  template <typename Op>
  static auto Lookup(absl::string_view key, Op* op)
      -> decltype(op->NotFound(key)) {
    return op->NotFound(key);
  }
};

// Helper to take a slice to a memento to a value.
// By splitting this part out we can scale code size as the number of (memento,
// value) types, rather than as the number of traits.
template <typename ParseMementoFn, typename MementoToValueFn>
struct ParseValue {
  template <ParseMementoFn parse_memento, MementoToValueFn memento_to_value>
  static GPR_ATTRIBUTE_NOINLINE auto Parse(Slice* value,
                                           MetadataParseErrorFn on_error)
      -> decltype(memento_to_value(parse_memento(std::move(*value),
                                                 on_error))) {
    return memento_to_value(parse_memento(std::move(*value), on_error));
  }
};

// This is an "Op" type for NameLookup.
// Used for MetadataMap::Parse, its Found/NotFound methods turn a slice into a
// ParsedMetadata object.
template <typename Container>
class ParseHelper {
 public:
  ParseHelper(Slice value, MetadataParseErrorFn on_error, size_t transport_size)
      : value_(std::move(value)),
        on_error_(on_error),
        transport_size_(transport_size) {}

  template <typename Trait>
  GPR_ATTRIBUTE_NOINLINE ParsedMetadata<Container> Found(Trait trait) {
    return ParsedMetadata<Container>(
        trait,
        ParseValueToMemento<typename Trait::MementoType, Trait::ParseMemento>(),
        transport_size_);
  }

  GPR_ATTRIBUTE_NOINLINE ParsedMetadata<Container> NotFound(
      absl::string_view key) {
    return ParsedMetadata<Container>(Slice::FromCopiedString(key),
                                     std::move(value_));
  }

 private:
  template <typename T, T (*parse_memento)(Slice, MetadataParseErrorFn)>
  GPR_ATTRIBUTE_NOINLINE T ParseValueToMemento() {
    return parse_memento(std::move(value_), on_error_);
  }

  Slice value_;
  MetadataParseErrorFn on_error_;
  const size_t transport_size_;
};

// This is an "Op" type for NameLookup.
// Used for MetadataMap::Append, its Found/NotFound methods turn a slice into a
// value and add it to a container.
template <typename Container>
class AppendHelper {
 public:
  AppendHelper(Container* container, Slice value, MetadataParseErrorFn on_error)
      : container_(container), value_(std::move(value)), on_error_(on_error) {}

  template <typename Trait>
  GPR_ATTRIBUTE_NOINLINE void Found(Trait trait) {
    container_->Set(
        trait, ParseValue<decltype(Trait::ParseMemento),
                          decltype(Trait::MementoToValue)>::
                   template Parse<Trait::ParseMemento, Trait::MementoToValue>(
                       &value_, on_error_));
  }

  GPR_ATTRIBUTE_NOINLINE void NotFound(absl::string_view key) {
    container_->AppendUnknown(key, std::move(value_));
  }

 private:
  Container* const container_;
  Slice value_;
  MetadataParseErrorFn on_error_;
};

// This is an "Op" type for NameLookup.
// Used for MetadataMap::Remove, its Found/NotFound methods remove a key from
// the container.
template <typename Container>
class RemoveHelper {
 public:
  explicit RemoveHelper(Container* container) : container_(container) {}

  template <typename Trait>
  GPR_ATTRIBUTE_NOINLINE void Found(Trait trait) {
    container_->Remove(trait);
  }

  GPR_ATTRIBUTE_NOINLINE void NotFound(absl::string_view key) {
    container_->RemoveUnknown(key);
  }

 private:
  Container* const container_;
};

// This is an "Op" type for NameLookup.
// Used for MetadataMap::GetStringValue, its Found/NotFound methods generated a
// string value from the container.
template <typename Container>
class GetStringValueHelper {
 public:
  explicit GetStringValueHelper(const Container* container,
                                std::string* backing)
      : container_(container), backing_(backing) {}

  template <typename Trait>
  GPR_ATTRIBUTE_NOINLINE absl::enable_if_t<
      Trait::kRepeatable == false &&
          std::is_same<Slice, typename Trait::ValueType>::value,
      absl::optional<absl::string_view>>
  Found(Trait) {
    const auto* value = container_->get_pointer(Trait());
    if (value == nullptr) return absl::nullopt;
    return value->as_string_view();
  }

  template <typename Trait>
  GPR_ATTRIBUTE_NOINLINE absl::enable_if_t<
      Trait::kRepeatable == true &&
          !std::is_same<Slice, typename Trait::ValueType>::value,
      absl::optional<absl::string_view>>
  Found(Trait) {
    const auto* value = container_->get_pointer(Trait());
    if (value == nullptr) return absl::nullopt;
    backing_->clear();
    for (const auto& v : *value) {
      if (!backing_->empty()) backing_->push_back(',');
      auto new_segment = Trait::Encode(v);
      backing_->append(new_segment.begin(), new_segment.end());
    }
    return *backing_;
  }

  template <typename Trait>
  GPR_ATTRIBUTE_NOINLINE absl::enable_if_t<
      Trait::kRepeatable == false &&
          !std::is_same<Slice, typename Trait::ValueType>::value,
      absl::optional<absl::string_view>>
  Found(Trait) {
    const auto* value = container_->get_pointer(Trait());
    if (value == nullptr) return absl::nullopt;
    *backing_ = std::string(Trait::Encode(*value).as_string_view());
    return *backing_;
  }

  GPR_ATTRIBUTE_NOINLINE absl::optional<absl::string_view> NotFound(
      absl::string_view key) {
    return container_->GetStringValueUnknown(key, backing_);
  }

 private:
  const Container* const container_;
  std::string* backing_;
};

// Generate a strong type for metadata values per trait.
template <typename Which, typename Ignored = void>
struct Value;

template <typename Which>
struct Value<Which, absl::enable_if_t<Which::kRepeatable == false, void>> {
  Value() = default;
  explicit Value(const typename Which::ValueType& value) : value(value) {}
  explicit Value(typename Which::ValueType&& value)
      : value(std::forward<typename Which::ValueType>(value)) {}
  Value(const Value&) = delete;
  Value& operator=(const Value&) = delete;
  Value(Value&&) noexcept = default;
  Value& operator=(Value&& other) noexcept {
    value = std::move(other.value);
    return *this;
  }
  template <typename Encoder>
  void EncodeTo(Encoder* encoder) const {
    encoder->Encode(Which(), value);
  }
  using StorageType = typename Which::ValueType;
  GPR_NO_UNIQUE_ADDRESS StorageType value;
};

template <typename Which>
struct Value<Which, absl::enable_if_t<Which::kRepeatable == true, void>> {
  Value() = default;
  explicit Value(const typename Which::ValueType& value) {
    this->value.push_back(value);
  }
  explicit Value(typename Which::ValueType&& value) {
    this->value.emplace_back(std::forward<typename Which::ValueType>(value));
  }
  Value(const Value&) = delete;
  Value& operator=(const Value&) = delete;
  Value(Value&& other) noexcept : value(std::move(other.value)) {}
  Value& operator=(Value&& other) noexcept {
    value = std::move(other.value);
    return *this;
  }
  template <typename Encoder>
  void EncodeTo(Encoder* encoder) const {
    for (const auto& v : value) {
      encoder->Encode(Which(), v);
    }
  }
  using StorageType = absl::InlinedVector<typename Which::ValueType, 1>;
  StorageType value;
};

// Encoder to copy some metadata
template <typename Output>
class CopySink {
 public:
  explicit CopySink(Output* dst) : dst_(dst) {}

  template <class T, class V>
  void Encode(T trait, V value) {
    dst_->Set(trait, value);
  }

  template <class T>
  void Encode(T trait, const Slice& value) {
    dst_->Set(trait, std::move(value.AsOwned()));
  }

  void Encode(const Slice& key, const Slice& value) {
    dst_->AppendUnknown(key.as_string_view(), value.Ref());
  }

 private:
  Output* dst_;
};

}  // namespace metadata_detail

// Helper function for encoders
// Given a metadata trait, convert the value to a slice.
template <typename Which>
absl::enable_if_t<std::is_same<typename Which::ValueType, Slice>::value,
                  const Slice&>
MetadataValueAsSlice(const Slice& slice) {
  return slice;
}

template <typename Which>
absl::enable_if_t<!std::is_same<typename Which::ValueType, Slice>::value, Slice>
MetadataValueAsSlice(typename Which::ValueType value) {
  return Slice(Which::Encode(value));
}

// MetadataMap encodes the mapping of metadata keys to metadata values.
//
// MetadataMap takes a derived class and list of traits. Each of these trait
// objects defines one metadata field that is used by core, and so should have
// more specialized handling than just using the generic APIs.
//
// MetadataMap is the backing type for some derived type via the curiously
// recursive template pattern. This is because many types consumed by
// MetadataMap require the container type to operate on, and many of those
// types are instantiated one per trait. A naive implementation without the
// Derived type would, for traits A,B,C, then instantiate for some
// T<Container, Trait>:
//  - T<MetadataMap<A,B,C>, A>,
//  - T<MetadataMap<A,B,C>, B>,
//  - T<MetadataMap<A,B,C>, C>.
// Since these types ultimately need to be recorded in the .dynstr segment
// for dynamic linkers (if gRPC is linked as a static library) this would
// create O(N^2) bytes of symbols even in stripped libraries. To avoid this
// we use the derived type (e.g. grpc_metadata_batch right now) to capture
// the container type, and we would write T<grpc_metadata_batch, A>, etc...
// Note that now the container type uses a number of bytes that is independent
// of the number of traits, and so we return to a linear symbol table growth
// function.
//
// Each trait object has the following signature:
// // Traits for the grpc-xyz metadata field:
// struct GrpcXyzMetadata {
//   // The type that's stored on MetadataBatch
//   using ValueType = ...;
//   // Can this metadata field be repeated?
//   static constexpr bool kRepeatable = ...;
//   // The type that's stored in compression/decompression tables
//   using MementoType = ...;
//   // The string key for this metadata type (for transports that require it)
//   static absl::string_view key() { return "grpc-xyz"; }
//   // Parse a memento from a slice
//   // Takes ownership of value
//   // Calls fn in the case of an error that should be reported to the user
//   static MementoType ParseMemento(Slice value, MementoParseErrorFn fn) { ...
//   }
//   // Convert a memento to a value
//   static ValueType MementoToValue(MementoType memento) { ... }
//   // Convert a value to its canonical text wire format (the format that
//   // ParseMemento will accept!)
//   static Slice Encode(const ValueType& value);
//   // Convert a value to something that can be passed to StrCat and displayed
//   // for debugging
//   static SomeStrCatableType DisplayValue(MementoType value) { ... }
// };
//
// About parsing and mementos:
//
// Many gRPC transports exchange metadata as key/value strings, but also allow
// for a more efficient representation as a single integer. We can use this
// integer representation to avoid reparsing too, by storing the parsed value
// in the compression table. This is what mementos are used for.
//
// A trait offers the capability to turn a slice into a memento via
// ParseMemento. This is exposed to users of MetadataMap via the Parse() method,
// that returns a ParsedMetadata object. That ParsedMetadata object can in turn
// be used to set the same value on many different MetadataMaps without having
// to reparse.
//
// Implementation wise, ParsedMetadata is a type erased wrapper around
// MementoType. When we set a value on MetadataMap, we first turn that memento
// into a value. For most types, this is going to be a no-op, but for example
// for grpc-timeout we make the memento the timeout expressed on the wire, but
// we make the value the timestamp of when the timeout will expire (i.e. the
// deadline).
template <class Derived, typename... Traits>
class MetadataMap {
 public:
  explicit MetadataMap(Arena* arena);
  ~MetadataMap();

  MetadataMap(const MetadataMap&) = delete;
  MetadataMap& operator=(const MetadataMap&) = delete;
  MetadataMap(MetadataMap&&) noexcept;
  // We never create MetadataMap directly, instead we create Derived, but we
  // want to be able to move it without redeclaring this.
  // NOLINTNEXTLINE(misc-unconventional-assign-operator)
  Derived& operator=(MetadataMap&&) noexcept;

  // Encode this metadata map into some encoder.
  // For each field that is set in the MetadataMap, call
  // encoder->Encode.
  //
  // For fields for which we have traits, this will be a method with
  // the signature:
  //    void Encode(TraitsType, typename TraitsType::ValueType value);
  // For fields for which we do not have traits, this will be a method
  // with the signature:
  //    void Encode(grpc_mdelem md);
  // TODO(ctiller): It's expected that the latter Encode method will
  // become Encode(Slice, Slice) by the end of the current metadata API
  // transitions.
  template <typename Encoder>
  void Encode(Encoder* encoder) const {
    table_.ForEach(EncodeWrapper<Encoder>{encoder});
    for (const auto& unk : unknown_) {
      encoder->Encode(unk.first, unk.second);
    }
  }

  // Similar to Encode, but targeted at logging: for each metadatum,
  // call f(key, value) as absl::string_views.
  void Log(absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn)
      const;

  // Get the pointer to the value of some known metadata.
  // Returns nullptr if the metadata is not present.
  // Causes a compilation error if Which is not an element of Traits.
  template <typename Which>
  const typename metadata_detail::Value<Which>::StorageType* get_pointer(
      Which) const {
    if (auto* p = table_.template get<Value<Which>>()) return &p->value;
    return nullptr;
  }

  // Get the pointer to the value of some known metadata.
  // Returns nullptr if the metadata is not present.
  // Causes a compilation error if Which is not an element of Traits.
  template <typename Which>
  typename metadata_detail::Value<Which>::StorageType* get_pointer(Which) {
    if (auto* p = table_.template get<Value<Which>>()) return &p->value;
    return nullptr;
  }

  // Get the pointer to the value of some known metadata.
  // Adds the default value for the metadata is not present.
  // Causes a compilation error if Which is not an element of Traits.
  template <typename Which>
  typename metadata_detail::Value<Which>::StorageType* GetOrCreatePointer(
      Which) {
    return &table_.template get_or_create<Value<Which>>()->value;
  }

  // Get the value of some known metadata.
  // Returns nullopt if the metadata is not present.
  // Causes a compilation error if Which is not an element of Traits.
  template <typename Which>
  absl::optional<typename Which::ValueType> get(Which) const {
    if (auto* p = table_.template get<Value<Which>>()) return p->value;
    return absl::nullopt;
  }

  // Set the value of some known metadata.
  // Returns a pointer to the new value.
  template <typename Which, typename... Args>
  absl::enable_if_t<Which::kRepeatable == false, void> Set(Which,
                                                           Args&&... args) {
    table_.template set<Value<Which>>(std::forward<Args>(args)...);
  }
  template <typename Which, typename... Args>
  absl::enable_if_t<Which::kRepeatable == true, void> Set(Which,
                                                          Args&&... args) {
    GetOrCreatePointer(Which())->emplace_back(std::forward<Args>(args)...);
  }

  // Remove a specific piece of known metadata.
  template <typename Which>
  void Remove(Which) {
    table_.template clear<Value<Which>>();
  }

  // Remove some metadata by name
  void Remove(absl::string_view key) {
    metadata_detail::RemoveHelper<Derived> helper(static_cast<Derived*>(this));
    metadata_detail::NameLookup<Traits...>::Lookup(key, &helper);
  }

  void Remove(const char* key) { Remove(absl::string_view(key)); }

  // Retrieve some metadata by name
  absl::optional<absl::string_view> GetStringValue(absl::string_view name,
                                                   std::string* buffer) const {
    metadata_detail::GetStringValueHelper<Derived> helper(
        static_cast<const Derived*>(this), buffer);
    return metadata_detail::NameLookup<Traits...>::Lookup(name, &helper);
  }

  // Extract a piece of known metadata.
  // Returns nullopt if the metadata was not present, or the value if it was.
  // The same as:
  //  auto value = m.get(T());
  //  m.Remove(T());
  template <typename Which>
  absl::enable_if_t<Which::kRepeatable == false,
                    absl::optional<typename Which::ValueType>>
  Take(Which which) {
    if (auto* p = get_pointer(which)) {
      absl::optional<typename Which::ValueType> value(std::move(*p));
      Remove(which);
      return value;
    }
    return {};
  }

  // Extract repeated known metadata.
  // Returns an empty vector if the metadata was not present.
  template <typename Which>
  absl::enable_if_t<Which::kRepeatable == true,
                    typename metadata_detail::Value<Which>::StorageType>
  Take(Which which) {
    if (auto* p = get_pointer(which)) {
      typename Value<Which>::StorageType value = std::move(*p);
      Remove(which);
      return value;
    }
    return {};
  }

  // Parse metadata from a key/value pair, and return an object representing
  // that result.
  // TODO(ctiller): key should probably be an absl::string_view.
  // Once we don't care about interning anymore, make that change!
  static ParsedMetadata<Derived> Parse(absl::string_view key, Slice value,
                                       uint32_t transport_size,
                                       MetadataParseErrorFn on_error) {
    metadata_detail::ParseHelper<Derived> helper(value.TakeOwned(), on_error,
                                                 transport_size);
    return metadata_detail::NameLookup<Traits...>::Lookup(key, &helper);
  }

  // Set a value from a parsed metadata object.
  void Set(const ParsedMetadata<Derived>& m) {
    m.SetOnContainer(static_cast<Derived*>(this));
  }

  // Append a key/value pair - takes ownership of value
  void Append(absl::string_view key, Slice value,
              MetadataParseErrorFn on_error) {
    metadata_detail::AppendHelper<Derived> helper(static_cast<Derived*>(this),
                                                  value.TakeOwned(), on_error);
    metadata_detail::NameLookup<Traits...>::Lookup(key, &helper);
  }

  void Clear();
  size_t TransportSize() const;
  Derived Copy() const;
  bool empty() const { return table_.empty() && unknown_.empty(); }
  size_t count() const { return table_.count() + unknown_.size(); }

 private:
  friend class metadata_detail::AppendHelper<Derived>;
  friend class metadata_detail::GetStringValueHelper<Derived>;
  friend class metadata_detail::RemoveHelper<Derived>;
  friend class metadata_detail::CopySink<Derived>;
  friend class ParsedMetadata<Derived>;

  template <typename Which>
  using Value = metadata_detail::Value<Which>;

  // Callable for the ForEach in Encode() -- for each value, call the
  // appropriate encoder method.
  template <typename Encoder>
  struct EncodeWrapper {
    Encoder* encoder;
    template <typename Which>
    void operator()(const Value<Which>& which) {
      which.EncodeTo(encoder);
    }
  };

  // Encoder to compute TransportSize
  class TransportSizeEncoder {
   public:
    void Encode(const Slice& key, const Slice& value) {
      size_ += key.length() + value.length() + 32;
    }

    template <typename Which>
    void Encode(Which, const typename Which::ValueType& value) {
      Add(Which(), value);
    }

    void Encode(ContentTypeMetadata,
                const typename ContentTypeMetadata::ValueType& value) {
      if (value == ContentTypeMetadata::kInvalid) return;
      Add(ContentTypeMetadata(), value);
    }

    size_t size() const { return size_; }

   private:
    template <typename Which>
    void Add(Which, const typename Which::ValueType& value) {
      size_ += Which::key().length() + Which::Encode(value).length() + 32;
    }

    uint32_t size_ = 0;
  };

  // Encoder to log some metadata
  class LogEncoder {
   public:
    explicit LogEncoder(
        absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn)
        : log_fn_(log_fn) {}

    template <typename Which>
    void Encode(Which, const typename Which::ValueType& value) {
      log_fn_(Which::key(), absl::StrCat(Which::DisplayValue(value)));
    }

    void Encode(const Slice& key, const Slice& value) {
      log_fn_(key.as_string_view(), value.as_string_view());
    }

   private:
    absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn_;
  };

  void AppendUnknown(absl::string_view key, Slice value) {
    unknown_.EmplaceBack(Slice::FromCopiedString(key), value.Ref());
  }

  void RemoveUnknown(absl::string_view key) {
    unknown_.SetEnd(std::remove_if(unknown_.begin(), unknown_.end(),
                                   [key](const std::pair<Slice, Slice>& p) {
                                     return p.first.as_string_view() == key;
                                   }));
  }

  absl::optional<absl::string_view> GetStringValueUnknown(
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

  // Table of known metadata types.
  Table<Value<Traits>...> table_;
  // Backing store for added metadata.
  ChunkedVector<std::pair<Slice, Slice>, 10> unknown_;
};

// Ok/not-ok check for metadata maps that contain GrpcStatusMetadata, so that
// they can be used as result types for TrySeq.
template <typename Derived, typename... Args>
inline bool IsStatusOk(const MetadataMap<Derived, Args...>& m) {
  return m.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN) ==
         GRPC_STATUS_OK;
}

template <typename Derived, typename... Traits>
MetadataMap<Derived, Traits...>::MetadataMap(Arena* arena) : unknown_(arena) {}

template <typename Derived, typename... Traits>
MetadataMap<Derived, Traits...>::MetadataMap(MetadataMap&& other) noexcept
    : table_(std::move(other.table_)), unknown_(std::move(other.unknown_)) {}

// We never create MetadataMap directly, instead we create Derived, but we want
// to be able to move it without redeclaring this.
// NOLINTNEXTLINE(misc-unconventional-assign-operator)
template <typename Derived, typename... Traits>
Derived& MetadataMap<Derived, Traits...>::operator=(
    MetadataMap&& other) noexcept {
  table_ = std::move(other.table_);
  unknown_ = std::move(other.unknown_);
  return static_cast<Derived&>(*this);
}

template <typename Derived, typename... Traits>
MetadataMap<Derived, Traits...>::~MetadataMap() = default;

template <typename Derived, typename... Traits>
void MetadataMap<Derived, Traits...>::Clear() {
  table_.ClearAll();
  unknown_.Clear();
}

template <typename Derived, typename... Traits>
size_t MetadataMap<Derived, Traits...>::TransportSize() const {
  TransportSizeEncoder enc;
  Encode(&enc);
  return enc.size();
}

template <typename Derived, typename... Traits>
Derived MetadataMap<Derived, Traits...>::Copy() const {
  Derived out(unknown_.arena());
  metadata_detail::CopySink<Derived> sink(&out);
  Encode(&sink);
  return out;
}

template <typename Derived, typename... Traits>
void MetadataMap<Derived, Traits...>::Log(
    absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn)
    const {
  LogEncoder enc(log_fn);
  Encode(&enc);
}

}  // namespace grpc_core

struct grpc_metadata_batch;

using grpc_metadata_batch_base = grpc_core::MetadataMap<
    grpc_metadata_batch,
    // Colon prefixed headers first
    grpc_core::HttpPathMetadata, grpc_core::HttpAuthorityMetadata,
    grpc_core::HttpMethodMetadata, grpc_core::HttpStatusMetadata,
    grpc_core::HttpSchemeMetadata,
    // Non-colon prefixed headers begin here
    grpc_core::ContentTypeMetadata, grpc_core::TeMetadata,
    grpc_core::GrpcEncodingMetadata, grpc_core::GrpcInternalEncodingRequest,
    grpc_core::GrpcAcceptEncodingMetadata, grpc_core::GrpcStatusMetadata,
    grpc_core::GrpcTimeoutMetadata, grpc_core::GrpcPreviousRpcAttemptsMetadata,
    grpc_core::GrpcRetryPushbackMsMetadata, grpc_core::UserAgentMetadata,
    grpc_core::GrpcMessageMetadata, grpc_core::HostMetadata,
    grpc_core::XEndpointLoadMetricsBinMetadata,
    grpc_core::GrpcServerStatsBinMetadata, grpc_core::GrpcTraceBinMetadata,
    grpc_core::GrpcTagsBinMetadata, grpc_core::GrpcLbClientStatsMetadata,
    grpc_core::LbCostBinMetadata, grpc_core::LbTokenMetadata>;

struct grpc_metadata_batch : public grpc_metadata_batch_base {
  using grpc_metadata_batch_base::grpc_metadata_batch_base;
};

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H */
