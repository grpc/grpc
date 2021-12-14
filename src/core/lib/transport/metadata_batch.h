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
  static absl::string_view key() { return "grpc-encoding"; }
};

// grpc-internal-encoding-request metadata trait.
struct GrpcInternalEncodingRequest : public CompressionAlgorithmBasedMetadata {
  static absl::string_view key() { return "grpc-internal-encoding-request"; }
};

// grpc-accept-encoding metadata trait.
struct GrpcAcceptEncodingMetadata {
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
  static absl::string_view key() { return "user-agent"; }
};

// grpc-message metadata trait.
struct GrpcMessageMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return "grpc-message"; }
};

// host metadata trait.
struct HostMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return "host"; }
};

// x-endpoint-load-metrics-bin metadata trait.
struct XEndpointLoadMetricsBinMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return "x-endpoint-load-metrics-bin"; }
};

// grpc-server-stats-bin metadata trait.
struct GrpcServerStatsBinMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return "grpc-server-stats-bin"; }
};

// grpc-trace-bin metadata trait.
struct GrpcTraceBinMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return "grpc-trace-bin"; }
};

// grpc-tags-bin metadata trait.
struct GrpcTagsBinMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return "grpc-tags-bin"; }
};

// :authority metadata trait.
struct HttpAuthorityMetadata : public SimpleSliceBasedMetadata {
  static absl::string_view key() { return ":authority"; }
};

// :path metadata trait.
struct HttpPathMetadata : public SimpleSliceBasedMetadata {
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
  static absl::string_view key() { return "grpc-status"; }
};

// grpc-previous-rpc-attempts metadata trait.
struct GrpcPreviousRpcAttemptsMetadata
    : public SimpleIntBasedMetadata<uint32_t, 0> {
  static absl::string_view key() { return "grpc-previous-rpc-attempts"; }
};

// grpc-retry-pushback-ms metadata trait.
struct GrpcRetryPushbackMsMetadata
    : public SimpleIntBasedMetadata<grpc_millis, GRPC_MILLIS_INF_PAST> {
  static absl::string_view key() { return "grpc-retry-pushback-ms"; }
};

// :status metadata trait.
// TODO(ctiller): consider moving to uint16_t
struct HttpStatusMetadata : public SimpleIntBasedMetadata<uint32_t, 0> {
  static absl::string_view key() { return ":status"; }
};

// "secret" metadata trait used to pass load balancing token between filters.
// This should not be exposed outside of gRPC core.
class GrpcLbClientStats;
struct GrpcLbClientStatsMetadata {
  static absl::string_view key() { return "grpclb_client_stats"; }
  using ValueType = GrpcLbClientStats*;
  using MementoType = ValueType;
  static ValueType MementoToValue(MementoType value) { return value; }
  static Slice Encode(ValueType x) { abort(); }
  static const char* DisplayValue(MementoType x) {
    return "<internal-lb-stats>";
  }
  static MementoType ParseMemento(Slice, MetadataParseErrorFn) { return nullptr; }
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
// Used for MetadataMap::Parse, its Found/NotFound methods turn a slice into a
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
// Right now the API presented is the minimal one that will allow us to
// substitute this type for grpc_metadata_batch in a relatively easy fashion. At
// that point we'll start iterating this API into something that's ergonomic
// again, whilst minimally holding the performance bar already set (and
// hopefully improving some things).
// In the meantime, we're not going to invest much time in ephemeral API
// documentation, so if you must use one of these APIs and it's not obvious
// how, reach out to ctiller.
//
// MetadataMap takes a list of traits. Each of these trait objects defines
// one metadata field that is used by core, and so should have more specialized
// handling than just using the generic APIs.
//
// Each trait object has the following signature:
// // Traits for the grpc-xyz metadata field:
// struct GrpcXyzMetadata {
//   // The type that's stored on MetadataBatch
//   using ValueType = ...;
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
template <typename... Traits>
class MetadataMap {
 public:
  explicit MetadataMap(Arena* arena);
  ~MetadataMap();

  MetadataMap(const MetadataMap&) = delete;
  MetadataMap& operator=(const MetadataMap&) = delete;
  MetadataMap(MetadataMap&&) noexcept;
  MetadataMap& operator=(MetadataMap&&) noexcept;

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
  void Log(absl::FunctionRef<void(absl::string_view, absl::string_view)> f) const;

  // Get the pointer to the value of some known metadata.
  // Returns nullptr if the metadata is not present.
  // Causes a compilation error if Which is not an element of Traits.
  template <typename Which>
  const typename Which::ValueType* get_pointer(Which) const {
    if (auto* p = table_.template get<Value<Which>>()) return &p->value;
    return nullptr;
  }

  // Get the pointer to the value of some known metadata.
  // Returns nullptr if the metadata is not present.
  // Causes a compilation error if Which is not an element of Traits.
  template <typename Which>
  typename Which::ValueType* get_pointer(Which) {
    if (auto* p = table_.template get<Value<Which>>()) return &p->value;
    return nullptr;
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
  typename Which::ValueType* Set(Which, Args&&... args) {
    return &table_.template set<Value<Which>>(std::forward<Args>(args)...)
                ->value;
  }

  // Remove a specific piece of known metadata.
  template <typename Which>
  void Remove(Which) {
    table_.template clear<Value<Which>>();
  }

  // Remove some metadata by name
  void Remove(absl::string_view name);

  // Retrieve some metadata by name
  absl::optional<absl::string_view> GetStringValue(absl::string_view name,
                                                   std::string* buffer) const;

  // Extract a piece of known metadata.
  // Returns nullopt if the metadata was not present, or the value if it was.
  // The same as:
  //  auto value = m.get(T());
  //  m.Remove(T());
  template <typename Which>
  absl::optional<typename Which::ValueType> Take(Which which) {
    if (auto* p = get_pointer(which)) {
      absl::optional<typename Which::ValueType> value(std::move(*p));
      Remove(which);
      return value;
    }
    return {};
  }

  // Parse metadata from a key/value pair, and return an object representing
  // that result.
  // TODO(ctiller): key should probably be an absl::string_view.
  // Once we don't care about interning anymore, make that change!
  static ParsedMetadata<MetadataMap> Parse(absl::string_view key, Slice value,
                                           uint32_t transport_size,
                                           MetadataParseErrorFn on_error) {
    metadata_detail::ParseHelper<MetadataMap> helper(value.TakeOwned(),
                                                     on_error, transport_size);
    return metadata_detail::NameLookup<Traits...>::Lookup(key, &helper);
  }

  // Set a value from a parsed metadata object.
  void
  Set(const ParsedMetadata<MetadataMap>& m) {
    m.SetOnContainer(this);
  }

  // Append a key/value pair - takes ownership of value
  void Append(absl::string_view key, Slice value,
              MetadataParseErrorFn on_error) {
    metadata_detail::AppendHelper<MetadataMap> helper(this, value.TakeOwned(),
                                                      on_error);
    metadata_detail::NameLookup<Traits...>::Lookup(key, &helper);
  }

  void Clear();
  size_t TransportSize() const;
  MetadataMap Copy() const;
  bool empty() const;
  size_t count() const;

 private:
  friend class metadata_detail::AppendHelper<MetadataMap>;

  // Generate a strong type for metadata values per trait.
  template <typename Which>
  struct Value {
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
    GPR_NO_UNIQUE_ADDRESS typename Which::ValueType value;
  };
  // Callable for the ForEach in Encode() -- for each value, call the
  // appropriate encoder method.
  template <typename Encoder>
  struct EncodeWrapper {
    Encoder* encoder;
    template <typename Which>
    void operator()(const Value<Which>& which) {
      encoder->Encode(Which(), which.value);
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
      size_ += Which::key().length() + Which::Encode(value).length() + 32;
    }

    size_t size() const { return size_; }

   private:
    uint32_t size_ = 0;
  };

  void AppendUnknown(absl::string_view key, Slice value);

  // Table of known metadata types.
  Table<Value<Traits>...> table_;
  // Backing store for added metadata.
  ChunkedVector<std::pair<Slice, Slice>, 10> unknown_;
};

template <typename... Traits>
MetadataMap<Traits...>::MetadataMap(Arena* arena) : unknown_(arena) {}

template <typename... Traits>
MetadataMap<Traits...>::MetadataMap(MetadataMap&& other) noexcept
    : table_(std::move(other.table_)), unknown_(std::move(other.unknown_)) {}

template <typename... Traits>
MetadataMap<Traits...>& MetadataMap<Traits...>::operator=(
    MetadataMap&& other) noexcept {
  table_ = std::move(other.table_);
  unknown_ = std::move(other.unknown_);
  return *this;
}

template <typename... Traits>
MetadataMap<Traits...>::~MetadataMap() = default;

template <typename... Traits>
void MetadataMap<Traits...>::Clear() {
  table_.ClearAll();
  unknown_.Clear();
}

template <typename... Traits>
size_t MetadataMap<Traits...>::TransportSize() const {
  TransportSizeEncoder enc;
  Encode(&enc);
  return enc.size();
}

}  // namespace grpc_core

using grpc_metadata_batch = grpc_core::MetadataMap<
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
    grpc_core::GrpcTagsBinMetadata, grpc_core::GrpcLbClientStatsMetadata>;

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H */
