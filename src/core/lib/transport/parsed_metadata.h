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
      sizeof(typename Which::MementoType) <= sizeof(intptr_t);
};

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
        value_(static_cast<intptr_t>(value)),
        transport_size_(transport_size) {}
  template <typename Which>
  ParsedMetadata(
      Which,
      absl::enable_if_t<!metadata_detail::HasSimpleMemento<Which>::value,
                        typename Which::MementoType>
          value,
      uint32_t transport_size)
      : vtable_(ParsedMetadata::template NonTrivialTraitVTable<Which>()),
        value_(
            reinterpret_cast<intptr_t>(new typename Which::MementoType(value))),
        transport_size_(transport_size) {}
  // Takes ownership of elem
  explicit ParsedMetadata(grpc_mdelem elem)
      : vtable_(grpc_is_binary_header_internal(GRPC_MDKEY(elem))
                    ? MdelemVtable<true>()
                    : MdelemVtable<false>()),
        value_(static_cast<intptr_t>(elem.payload)),
        transport_size_(GRPC_MDELEM_LENGTH(elem)) {}
  ParsedMetadata() : vtable_(EmptyVTable()) {}
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
  ParsedMetadata WithNewValue(const grpc_slice& value) const {
    return vtable_->with_new_value(value_, value);
  }
  std::string DebugString() const { return vtable_->debug_string(value_); }

  // TODO(ctiller): move to transport
  static uint32_t TransportSize(uint32_t key_size, uint32_t value_size) {
    // TODO(ctiller): use hpack constant?
    return key_size + value_size + 32;
  }

 private:
  struct VTable {
    const bool is_binary_header;
    void (*const destroy)(intptr_t value);
    grpc_error_handle (*const set)(intptr_t value,
                                   MetadataContainer* container);
    ParsedMetadata (*const with_new_value)(intptr_t value,
                                           const grpc_slice& new_value);
    std::string (*debug_string)(intptr_t value);
  };

  static const VTable* EmptyVTable();
  template <typename Which>
  static const VTable* TrivialTraitVTable();
  template <typename Which>
  static const VTable* NonTrivialTraitVTable();
  template <bool kIsBinaryHeader>
  static const VTable* MdelemVtable();

  const VTable* vtable_;
  intptr_t value_;
  uint32_t transport_size_;
};

template <typename MetadataContainer>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::EmptyVTable() {
  static const VTable vtable = {
      false,
      // destroy
      [](intptr_t) {},
      // set
      [](intptr_t, MetadataContainer*) { return GRPC_ERROR_NONE; },
      // with_new_value
      [](intptr_t, const grpc_slice&) { return ParsedMetadata(); },
      // debug_string
      [](intptr_t) -> std::string { return "empty"; }};
  return &vtable;
}

template <typename MetadataContainer>
template <typename Which>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::TrivialTraitVTable() {
  static const VTable vtable = {
      absl::EndsWith(Which::key(), "-bin"),
      // destroy
      [](intptr_t) {},
      // set
      [](intptr_t value, MetadataContainer* map) {
        map->Set(Which(), Which::MementoToValue(
                              static_cast<typename Which::MementoType>(value)));
        return GRPC_ERROR_NONE;
      },
      // with_new_value
      [](intptr_t, const grpc_slice& value) {
        return ParsedMetadata(
            Which(), Which::ParseMemento(value),
            TransportSize(strlen(Which::key()), GRPC_SLICE_LENGTH(value)));
      },
      // debug_string
      [](intptr_t value) {
        return absl::StrCat(Which::key(), ": ", Which::DisplayValue(value));
      }};
  return &vtable;
}

template <typename MetadataContainer>
template <typename Which>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::NonTrivialTraitVTable() {
  static const VTable vtable = {
      absl::EndsWith(Which::key(), "-bin"),
      // destroy
      [](intptr_t value) {
        delete reinterpret_cast<typename Which::MementoType*>(value);
      },
      // set
      [](intptr_t value, MetadataContainer* map) {
        auto* p = reinterpret_cast<typename Which::MementoType*>(value);
        map->Set(Which(), Which::MementoToValue(*p));
        return GRPC_ERROR_NONE;
      },
      // with_new_value
      [](intptr_t, const grpc_slice& value) {
        return ParsedMetadata(
            Which(), Which::ParseMemento(value),
            TransportSize(strlen(Which::key()), GRPC_SLICE_LENGTH(value)));
      },
      // debug_string
      [](intptr_t value) {
        auto* p = reinterpret_cast<typename Which::MementoType*>(value);
        return absl::StrCat(Which::key(), ": ", Which::DisplayValue(*p));
      }};
  return &vtable;
}

template <typename MetadataContainer>
template <bool kIsBinaryHeader>
const typename ParsedMetadata<MetadataContainer>::VTable*
ParsedMetadata<MetadataContainer>::MdelemVtable() {
  static const VTable vtable = {
      kIsBinaryHeader,
      // destroy
      [](intptr_t value) { GRPC_MDELEM_UNREF(grpc_mdelem{uintptr_t(value)}); },
      // set
      [](intptr_t value, MetadataContainer* map) {
        auto md = GRPC_MDELEM_REF(grpc_mdelem{uintptr_t(value)});
        auto err = map->Append(md);
        // If an error occurs, md is not consumed and we need to.
        // This is an awful API, but that's why we're replacing it.
        if (err != GRPC_ERROR_NONE) {
          GRPC_MDELEM_UNREF(md);
        }
        return err;
      },
      // with_new_value
      [](intptr_t value, const grpc_slice& value_slice) {
        grpc_mdelem elem{uintptr_t(value)};
        return ParsedMetadata(grpc_mdelem_from_slices(
            static_cast<const ManagedMemorySlice&>(
                grpc_slice_ref_internal(GRPC_MDKEY(elem))),
            value_slice));
      },
      // debug_string
      [](intptr_t value) {
        grpc_mdelem elem{uintptr_t(value)};
        return absl::StrCat(StringViewFromSlice(GRPC_MDKEY(elem)), ": ",
                            StringViewFromSlice(GRPC_MDVALUE(elem)));
      }};
  return &vtable;
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_TRANSPORT_PARSED_METADATA_H
