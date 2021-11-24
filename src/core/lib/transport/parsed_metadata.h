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

#ifndef GRPC_CORE_LIB_TRANSPORT_PARSED_METADATA_H
#define GRPC_CORE_LIB_TRANSPORT_PARSED_METADATA_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <type_traits>

#include "absl/meta/type_traits.h"
#include "absl/strings/match.h"

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

namespace metadata_detail {

// Helper to determine whether a traits metadata is inlinable inside a memento,
// or (if not) we'll need to take the memory allocation path.
template <typename Which>
struct HasSimpleMemento {
  static constexpr bool value =
      std::is_trivial<typename Which::MementoType>::value &&
      sizeof(typename Which::MementoType) <= sizeof(uint64_t);
};

// Storage type for a single metadata entry.
union Buffer {
  uint64_t trivial;
  void* pointer;
  grpc_slice slice;
  grpc_mdelem mdelem;
};

// Given a key and a value, concatenate together to make a debug string.
// Split out to avoid template bloat.
std::string MakeDebugString(absl::string_view key, absl::string_view value);

// Wrapper around MakeDebugString.
// For the value part, use two functions - one to extract a typed field from
// Buffer, and a second (sourced from the trait) to generate a displayable debug
// string from the field value. We try to maximize indirection/code sharing here
// as this is not critical path code and we'd like to avoid some code bloat -
// better to scale by number of types than then number of metadata traits!
template <typename Field, typename CompatibleWithField, typename Display>
GPR_ATTRIBUTE_NOINLINE std::string MakeDebugStringPipeline(
    absl::string_view key, const Buffer& value,
    Field (*field_from_buffer)(const Buffer&),
    Display (*display_from_field)(CompatibleWithField)) {
  return MakeDebugString(
      key, absl::StrCat(display_from_field(field_from_buffer(value))));
}

// Extract a trivial field value from a Buffer - for MakeDebugStringPipeline.
template <typename Field>
Field FieldFromTrivial(const Buffer& value) {
  return static_cast<Field>(value.trivial);
}

// Extract a pointer field value from a Buffer - for MakeDebugStringPipeline.
template <typename Field>
Field FieldFromPointer(const Buffer& value) {
  return *static_cast<const Field*>(value.pointer);
}

// Extract a Slice from a Buffer.
Slice SliceFromBuffer(const Buffer& buffer);

// Unref the grpc_slice part of a Buffer (assumes it is in fact a grpc_slice).
void DestroySliceValue(const Buffer& value);

}  // namespace metadata_detail

// A parsed metadata value.
// This type captures a type erased MementoType from one trait of
// MetadataContainer, and provides utilities to manipulate that and to set it on
// a MetadataContainer.
template <typename MetadataContainer>
class ParsedMetadata {
 public:
  // Construct metadata from a trait Which of MetadataContainer.
  // Two versions: the first is for simple inlinable mementos, and the second
  // forces an allocation.
  template <typename Which>
  ParsedMetadata(
      Which,
      absl::enable_if_t<metadata_detail::HasSimpleMemento<Which>::value,
                        typename Which::MementoType>
          value,
      uint32_t transport_size)
      : vtable_(ParsedMetadata::template TrivialTraitVTable<Which>()),
        transport_size_(transport_size) {
    value_.trivial = static_cast<uint64_t>(value);
  }
  template <typename Which>
  ParsedMetadata(
      Which,
      absl::enable_if_t<
          !metadata_detail::HasSimpleMemento<Which>::value &&
              !std::is_convertible<typename Which::MementoType, Slice>::value,
          typename Which::MementoType>
          value,
      uint32_t transport_size)
      : vtable_(ParsedMetadata::template NonTrivialTraitVTable<Which>()),
        transport_size_(transport_size) {
    value_.pointer = new typename Which::MementoType(std::move(value));
  }
  // Construct metadata from a Slice typed value.
  template <typename Which>
  ParsedMetadata(Which, Slice value, uint32_t transport_size)
      : vtable_(ParsedMetadata::template SliceTraitVTable<Which>()),
        transport_size_(transport_size) {
    value_.slice = value.TakeCSlice();
  }
  // Takes ownership of elem
  explicit ParsedMetadata(grpc_mdelem elem)
      : vtable_(grpc_is_binary_header_internal(GRPC_MDKEY(elem))
                    ? MdelemVtable<true>()
                    : MdelemVtable<false>()),
        transport_size_(GRPC_MDELEM_LENGTH(elem)) {
    value_.mdelem = elem;
  }
  ParsedMetadata() : vtable_(EmptyVTable()), transport_size_(0) {}
  ~ParsedMetadata() { vtable_->destroy(value_); }

  // Non copyable, but movable.
  ParsedMetadata(const ParsedMetadata&) = delete;
  ParsedMetadata& operator=(const ParsedMetadata&) = delete;
  ParsedMetadata(ParsedMetadata&& other) noexcept
      : vtable_(other.vtable_),
        value_(other.value_),
        transport_size_(other.transport_size_) {
    other.vtable_ = EmptyVTable();
  }
  ParsedMetadata& operator=(ParsedMetadata&& other) noexcept {
    vtable_ = other.vtable_;
    value_ = other.value_;
    transport_size_ = other.transport_size_;
    other.vtable_ = EmptyVTable();
    return *this;
  }

  // Set this parsed value on a container.
  GRPC_MUST_USE_RESULT grpc_error_handle
  SetOnContainer(MetadataContainer* container) const {
    return vtable_->set(value_, container);
  }

  // Is this a binary header or not?
  bool is_binary_header() const { return vtable_->is_binary_header; }
  // HTTP2 defined storage size of this metadatum.
  uint32_t transport_size() const { return transport_size_; }
  // Create a new parsed metadata with the same key but a different value.
  ParsedMetadata WithNewValue(Slice value) const {
    ParsedMetadata result;
    result.vtable_ = vtable_;
    result.value_ = value_;
    result.transport_size_ =
        TransportSize(vtable_->key_length(value_), value.length());
    vtable_->with_new_value(&value, &result);
    return result;
  }
  std::string DebugString() const { return vtable_->debug_string(value_); }

  // TODO(ctiller): move to transport
  static uint32_t TransportSize(uint32_t key_size, uint32_t value_size) {
    // TODO(ctiller): use hpack constant?
    return key_size + value_size + 32;
  }

 private:
  using Buffer = metadata_detail::Buffer;

  struct VTable {
    const bool is_binary_header;
    void (*const destroy)(const Buffer& value);
    grpc_error_handle (*const set)(const Buffer& value,
                                   MetadataContainer* container);
    // result is a bitwise copy of the originating ParsedMetadata.
    void (*const with_new_value)(Slice* new_value, ParsedMetadata* result);
    std::string (*const debug_string)(const Buffer& value);
    // TODO(ctiller): when we delete mdelem, make this a simple integer constant
    // on the vtable
    size_t (*const key_length)(const Buffer& value);
  };

  static const VTable* EmptyVTable();
  template <typename Which>
  static const VTable* TrivialTraitVTable();
  template <typename Which>
  static const VTable* NonTrivialTraitVTable();
  template <typename Which>
  static const VTable* SliceTraitVTable();
  template <bool kIsBinaryHeader>
  static const VTable* MdelemVtable();

  template <Slice (*ParseMemento)(Slice)>
  GPR_ATTRIBUTE_NOINLINE void WithNewValueSetSlice(Slice* slice) {
    value_.slice = ParseMemento(std::move(*slice)).TakeCSlice();
  }

  const VTable* vtable_;
  Buffer value_;
  uint32_t transport_size_;
};

namespace metadata_detail {}  // namespace metadata_detail

template <typename MetadataContainer>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::EmptyVTable() {
  static const VTable vtable = {
      false,
      // destroy
      [](const Buffer&) {},
      // set
      [](const Buffer&, MetadataContainer*) { return GRPC_ERROR_NONE; },
      // with_new_value
      [](Slice*, ParsedMetadata*) {},
      // debug_string
      [](const Buffer&) -> std::string { return "empty"; },
      // key_length
      [](const Buffer&) -> size_t { return 0; },
  };
  return &vtable;
}

template <typename MetadataContainer>
template <typename Which>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::TrivialTraitVTable() {
  static const VTable vtable = {
      absl::EndsWith(Which::key(), "-bin"),
      // destroy
      [](const Buffer&) {},
      // set
      [](const Buffer& value, MetadataContainer* map) {
        map->Set(Which(),
                 Which::MementoToValue(
                     static_cast<typename Which::MementoType>(value.trivial)));
        return GRPC_ERROR_NONE;
      },
      // with_new_value
      [](Slice* value, ParsedMetadata* result) {
        result->value_.trivial = Which::ParseMemento(std::move(*value));
      },
      // debug_string
      [](const Buffer& value) {
        return metadata_detail::MakeDebugStringPipeline(
            Which::key(), value,
            metadata_detail::FieldFromTrivial<typename Which::MementoType>,
            Which::DisplayValue);
      },
      // key_length
      [](const Buffer&) { return Which::key().size(); },
  };
  return &vtable;
}

template <typename MetadataContainer>
template <typename Which>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::NonTrivialTraitVTable() {
  static const VTable vtable = {
      absl::EndsWith(Which::key(), "-bin"),
      // destroy
      [](const Buffer& value) {
        delete static_cast<typename Which::MementoType*>(value.pointer);
      },
      // set
      [](const Buffer& value, MetadataContainer* map) {
        auto* p = static_cast<typename Which::MementoType*>(value.pointer);
        map->Set(Which(), Which::MementoToValue(*p));
        return GRPC_ERROR_NONE;
      },
      // with_new_value
      [](Slice* value, ParsedMetadata* result) {
        result->value_.pointer = new
            typename Which::MementoType(Which::ParseMemento(std::move(*value)));
      },
      // debug_string
      [](const Buffer& value) {
        return metadata_detail::MakeDebugStringPipeline(
            Which::key(), value,
            metadata_detail::FieldFromPointer<typename Which::MementoType>,
            Which::DisplayValue);
      },
      // key_length
      [](const Buffer&) { return Which::key().size(); },
  };
  return &vtable;
}

template <typename MetadataContainer>
template <typename Which>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::SliceTraitVTable() {
  static const VTable vtable = {
      absl::EndsWith(Which::key(), "-bin"),
      // destroy
      metadata_detail::DestroySliceValue,
      // set
      [](const Buffer& value, MetadataContainer* map) {
        map->Set(Which(), Which::MementoToValue(
                              metadata_detail::SliceFromBuffer(value)));
        return GRPC_ERROR_NONE;
      },
      // with_new_value
      [](Slice* value, ParsedMetadata* result) {
        result->WithNewValueSetSlice<Which::ParseMemento>(value);
      },
      // debug_string
      [](const Buffer& value) {
        return metadata_detail::MakeDebugStringPipeline(
            Which::key(), value, metadata_detail::SliceFromBuffer,
            Which::DisplayValue);
      },
      // key_length
      [](const Buffer&) { return Which::key().size(); },
  };
  return &vtable;
}

template <typename MetadataContainer>
template <bool kIsBinaryHeader>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::MdelemVtable() {
  static const VTable vtable = {
      kIsBinaryHeader,
      // destroy
      [](const Buffer& value) { GRPC_MDELEM_UNREF(value.mdelem); },
      // set
      [](const Buffer& value, MetadataContainer* map) {
        auto md = GRPC_MDELEM_REF(value.mdelem);
        auto err = map->Append(md);
        // If an error occurs, md is not consumed and we need to.
        // This is an awful API, but that's why we're replacing it.
        if (err != GRPC_ERROR_NONE) {
          GRPC_MDELEM_UNREF(md);
        }
        return err;
      },
      // with_new_value
      [](Slice* value_slice, ParsedMetadata* result) {
        result->value_.mdelem = grpc_mdelem_from_slices(
            static_cast<const ManagedMemorySlice&>(
                grpc_slice_ref_internal(GRPC_MDKEY(result->value_.mdelem))),
            value_slice->TakeCSlice());
      },
      // debug_string
      [](const Buffer& value) {
        return metadata_detail::MakeDebugString(
            StringViewFromSlice(GRPC_MDKEY(value.mdelem)),
            StringViewFromSlice(GRPC_MDVALUE(value.mdelem)));
      },
      // key_length
      [](const Buffer& value) {
        return GRPC_SLICE_LENGTH(GRPC_MDKEY(value.mdelem));
      }};
  return &vtable;
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_TRANSPORT_PARSED_METADATA_H
