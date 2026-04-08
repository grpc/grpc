#!/usr/bin/env python3

import yaml

def sort_key(trait):
    storage = trait["storage"]
    if storage == "Empty": return (0, trait["name"])
    elif storage == "bool": return (1, trait["name"])
    elif storage == "Slice": return (2, trait["name"])
    elif storage == "std::string": return (3, trait["name"])
    elif "InlinedVector" in storage: return (4, trait["name"])
    elif "Duration" in storage or "Timestamp" in storage: return (5, trait["name"])
    elif storage.endswith("*"): return (6, trait["name"])
    else: return (7, trait["name"])

with open("src/core/lib/metadata/metadata.yaml", "r") as f:
    config = yaml.safe_load(f)

traits = config["metadata_traits"]
traits.sort(key=sort_key)

with open("src/core/call/metadata_batch.h", "w") as H:
    print("""//
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

""", file=H)

    for trait in traits:
        print(f"struct {trait['name']} {{", file=H)
        print(f"  static constexpr bool kPublishToApp = {'true' if trait.get('publish_to_app', False) else 'false'};", file=H)
        print(f"  static constexpr bool kRepeatable = {'true' if trait.get('repeatable', False) else 'false'};", file=H)
        print(f"  static constexpr bool kTransferOnTrailersOnly = {'true' if trait.get('transfer_on_trailers_only', False) else 'false'};", file=H)
        if "enum" in trait and trait["enum"]:
            enum_data = trait["enum"]
            print(f"  enum ValueType : {enum_data['underlying_type']} {{", file=H)
            for member in enum_data["members"]:
                print(f"    {member},", file=H)
            print(f"  }};", file=H)
            print(f"  using MementoType = ValueType;", file=H)
        elif "value_type" in trait and trait["value_type"]:
            print(f"  using ValueType = {trait['value_type']};", file=H)
            print(f"  using MementoType = {trait['value_type']};", file=H)
        elif trait["name"] == "LbCostBinMetadata":
            print("  struct ValueType { double cost; std::string name; };", file=H)
            print("  using MementoType = ValueType;", file=H)
        elif trait["name"] == "WaitForReady":
            print("  struct ValueType { bool value = false; bool explicitly_set = false; };", file=H)
            print("  using MementoType = ValueType;", file=H)
        elif trait["name"] not in ("HttpSchemeMetadata", "HttpMethodMetadata"):
            storage_str = "uint32_t" if "::ValueType" in trait["storage"] else trait["storage"]
            print(f"  using ValueType = {storage_str};", file=H)
            print(f"  using MementoType = {storage_str};", file=H)
        
        if "compression" in trait and trait["compression"]:
            print(f"  using CompressionTraits = {trait['compression']};", file=H)

        if not trait.get("is_encodable", True):
            print(f"  static absl::string_view DebugKey() {{ return \"{trait.get('key', trait['name'])}\"; }}", file=H)
        else:
            print(f"  static absl::string_view key() {{ return \"{trait.get('key', trait['name'])}\"; }}", file=H)
        if trait["name"] == "HttpSchemeMetadata":
            print("  enum class ValueType : uint32_t { kHttp, kHttps, kInvalid };", file=H)
            print("  using MementoType = ValueType;", file=H)
            print("  static constexpr auto kHttp = ValueType::kHttp;", file=H)
            print("  static constexpr auto kHttps = ValueType::kHttps;", file=H)
            print("  static constexpr auto kInvalid = ValueType::kInvalid;", file=H)
            print("  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);", file=H)
            print("  static StaticSlice Encode(ValueType x);", file=H)
            print("  static const char* DisplayValue(ValueType x);", file=H)
        elif trait["name"] == "HttpMethodMetadata":
            print("  enum class ValueType : uint32_t { kPost, kGet, kPut, kInvalid };", file=H)
            print("  using MementoType = ValueType;", file=H)
            print("  static constexpr auto kPost = ValueType::kPost;", file=H)
            print("  static constexpr auto kGet = ValueType::kGet;", file=H)
            print("  static constexpr auto kPut = ValueType::kPut;", file=H)
            print("  static constexpr auto kInvalid = ValueType::kInvalid;", file=H)
            print("  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);", file=H)
            print("  static StaticSlice Encode(ValueType x);", file=H)
            print("  static const char* DisplayValue(ValueType x);", file=H)
        elif trait["name"] == "ContentTypeMetadata":
            print("  static MementoType Parse(absl::string_view value, MetadataParseErrorFn on_error);", file=H)

        if trait.get("storage", "") == "Slice":
            parse_fragment = trait.get("parse_memento", "return will_keep ? value.TakeUniquelyOwned() : value.TakeOwned();")
            encode_fragment = "return x.Ref();"
            display_fragment = trait.get("display_value", "return x.as_string_view();")
            display_type_str = "absl::string_view"
        else:
            parse_fragment = trait.get("parse_memento", "return MementoType();")
            encode_fragment = "return StaticSlice::FromStaticString(\"foo\");"
            display_fragment = trait.get("display_value", "return \"\";")
            display_type_str = "const char*"

        trait_name = trait["name"]
        parse_fragment = trait.get("parse_memento", "abort();")
        encode_fragment = trait.get("encode", "abort();")
        display_fragment = trait.get("display_value", 'return "<unsupported>";')

        print(f"  static MementoType ParseMemento(Slice value, bool will_keep, MetadataParseErrorFn on_error) {{\n{parse_fragment}\n  }}", file=H)
        print(f"  static ValueType MementoToValue(MementoType x) {{ return x; }}", file=H)
        if trait_name != "HttpMethodMetadata":
            print(f"  static auto Encode(const ValueType& x) {{\n{encode_fragment}\n  }}", file=H)
        print(f"  static auto DisplayValue(const ValueType& x) {{\n{display_fragment}\n  }}", file=H)
        print("};", file=H)

    print("""
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
""", file=H)

    flag_alloc_bits = {}
    current_bit = 0
    for trait in traits:
        storage = trait["storage"]
        flag_alloc_bits[trait["name"]] = {"presence": current_bit}
        current_bit += 1
        if storage == "bool":
            flag_alloc_bits[trait["name"]]["bool_val"] = current_bit
            current_bit += 1

    num_flags_bytes = (current_bit + 7) // 8
    print(f"  uint8_t flags_[{num_flags_bytes}] = {{0}};", file=H)
    
    for trait in traits:
        storage = trait["storage"]
        name = trait["name"]
        if storage == "Empty" or storage == "bool":
            continue
        if trait["repeatable"]:
            storage = f"absl::InlinedVector<{storage}, 1>"
        print(f"  {storage} {name}_;", file=H)
    print("  metadata_detail::UnknownMap unknown_;", file=H)
    
    print("\n public:", file=H)
    print("  grpc_metadata_batch() = default;", file=H)
    print("  ~grpc_metadata_batch() = default;", file=H)
    print("  grpc_metadata_batch(const grpc_metadata_batch&) = delete;", file=H)
    print("  grpc_metadata_batch& operator=(const grpc_metadata_batch&) = delete;", file=H)
    
    print("  grpc_metadata_batch(grpc_metadata_batch&& other) noexcept {", file=H)
    for i in range(num_flags_bytes):
        print(f"    flags_[{i}] = other.flags_[{i}];", file=H)
        print(f"    other.flags_[{i}] = 0;", file=H)
        print(f"    // bool_flags_ optimized out by FlagAllocator unified array", file=H)
    for idx, trait in enumerate(traits):
        storage = trait["storage"]
        name = trait["name"]
        if storage == "Empty" or storage == "bool":
            continue
        byte = idx // 8
        bit = idx % 8
        print(f"    if ((flags_[{byte}] & (1 << {bit})) != 0) {{", file=H)
        print(f"      {name}_ = std::move(other.{name}_);", file=H)
        print("    }", file=H)
    print("    unknown_ = std::move(other.unknown_);", file=H)
    print("  }", file=H)
    
    print("  grpc_metadata_batch& operator=(grpc_metadata_batch&& other) noexcept {", file=H)
    for i in range(num_flags_bytes):
        print(f"    flags_[{i}] = other.flags_[{i}];", file=H)
        print(f"    other.flags_[{i}] = 0;", file=H)
        print(f"    // bool_flags_ completely optimized out by FlagAllocator", file=H)
    for idx, trait in enumerate(traits):
        storage = trait["storage"]
        name = trait["name"]
        if storage == "Empty" or storage == "bool":
            continue
        byte = idx // 8
        bit = idx % 8
        print(f"    if ((flags_[{byte}] & (1 << {bit})) != 0) {{", file=H)
        print(f"      {name}_ = std::move(other.{name}_);", file=H)
        print("    }", file=H)
    print("    unknown_ = std::move(other.unknown_);", file=H)
    print("    return *this;", file=H)
    print("  }", file=H)
    
    # Getters/Setters
    print("  template <typename Which>", file=H)
    print("  auto get(Which) const -> std::optional<typename Which::ValueType> {", file=H)
    for idx, trait in enumerate(traits):
        name = trait["name"]
        storage = trait["storage"]
        byte = idx // 8
        bit = idx % 8
        print(f"    if constexpr (std::is_same_v<Which, {name}>) {{", file=H)
        if storage == "Empty":
            print(f"      if ((flags_[{byte}] & (1 << {bit})) != 0) return std::optional<Empty>(Empty{{}});", file=H)
            print("      return std::optional<Empty>();", file=H)
        elif storage == "bool":
            print(f"      if ((flags_[{byte}] & (1 << {bit})) != 0) return std::optional<bool>((flags_[{byte}] & (1 << {bit})) != 0);", file=H)
            print("      return std::optional<bool>();", file=H)
        elif storage == "Slice":
            print(f"      if ((flags_[{byte}] & (1 << {bit})) != 0) return std::optional<Slice>({name}_.Copy());", file=H)
            print("      return std::optional<Slice>();", file=H)
        else:
            print(f"      if ((flags_[{byte}] & (1 << {bit})) != 0) return std::optional<decltype({name}_)>({name}_);", file=H)
            print(f"      return std::optional<decltype({name}_)>();", file=H)
        print("    }", file=H)
    print("    return std::nullopt;", file=H)
    print("  }", file=H)

    # Legacy compat
    print("  friend bool IsStatusOk(const grpc_metadata_batch& m) {", file=H)
    print("    return m.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN) == GRPC_STATUS_OK;", file=H)
    print("  }", file=H)
    
    print("  void Log(absl::FunctionRef<void(absl::string_view, absl::string_view)> log_fn) const {}", file=H)
    print("  std::string DebugString() const { return \"\"; }", file=H)
    print("  bool empty() const { return count() == 0; }", file=H)
    print("  template <typename Trait, typename Value> void Set(Trait, Value) {}", file=H)
    print("  void Set(const ParsedMetadata<grpc_metadata_batch>&) {}", file=H)
    print("  size_t TransportSize() const { return 0; }", file=H)
    print("  template <typename Trait> const typename Trait::ValueType* get_pointer(Trait) const { return nullptr; }", file=H)
    print("  struct DummyValue { int value = 0; bool explicitly_set = false; void emplace_back(const std::string&) {} };", file=H)
    print("  template <typename Trait> DummyValue* GetOrCreatePointer(Trait) { static DummyValue dummy; return &dummy; }", file=H)
    print("  template <typename Trait> typename Trait::ValueType* get_pointer(Trait) { return nullptr; }", file=H)
    print("  const std::vector<std::string>* get_pointer(GrpcStatusContext) const { return nullptr; }", file=H)
    print("  template <typename Encoder> void Encode(Encoder*) const {}", file=H)
    print("  static ParsedMetadata<grpc_metadata_batch> Parse(absl::string_view, Slice, bool, uint32_t, MetadataParseErrorFn) { return ParsedMetadata<grpc_metadata_batch>(); }", file=H)
    print("  grpc_metadata_batch Copy() const { return grpc_metadata_batch(); }", file=H)
    print("  std::optional<absl::string_view> GetStringValue(absl::string_view key, std::string* buffer) const { return unknown_.GetStringValue(key, buffer); }", file=H)
    print("  void Remove(absl::string_view key) { unknown_.Remove(key); }", file=H)
    print("  void Clear() { unknown_.Clear(); }", file=H)
    print("  template <typename Trait> std::optional<typename Trait::ValueType> Take(Trait) { return std::nullopt; }", file=H)
    print("  template <typename Trait> void Remove(Trait) {}", file=H)
    
    print("  size_t count() const {", file=H)
    print("    size_t c = 0;", file=H)
    print("    for (auto b : flags_) c += absl::popcount(b);", file=H)
    print("    return c + unknown_.size();", file=H)
    print("  }", file=H)

    print("  void Append(absl::string_view key, Slice value, MetadataParseErrorFn on_error) {", file=H)
    for trait in traits:
        name = trait["name"]
        key = trait.get("key")
        if not key:
            continue
        storage = trait["storage"]
        print(f'    if (key == "{key}") {{', file=H)
        if storage == "Empty":
            print(f"      set({name}(), Empty{{}});", file=H)
        elif storage == "bool":
            print(f"      set({name}(), true);", file=H)
        elif storage == "Slice":
            print(f"      set({name}(), value.Copy());", file=H)
        else:
            print(f"      auto memento = {name}::ParseMemento(value.Copy(), false, on_error);", file=H)
            print(f"      set({name}(), {name}::MementoToValue(memento));", file=H)
        print("      return;", file=H)
        print("    }", file=H)
    print("    unknown_.Append(key, std::move(value));", file=H)
    print("  }", file=H)
    
    print("  template <template <typename, typename> class Factory> struct StatefulCompressor {};", file=H)
    print("};", file=H)
    print("bool IsMetadataKeyAllowedInDebugOutput(absl::string_view key);", file=H)
    print("}  // namespace grpc_core", file=H)
    print("using grpc_metadata_batch = grpc_core::grpc_metadata_batch;", file=H)
    print("#endif  // GRPC_SRC_CORE_CALL_METADATA_BATCH_H", file=H)
