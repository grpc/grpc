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

#ifndef GRPC_CORE_LIB_SLICE_SLICE_H
#define GRPC_CORE_LIB_SLICE_SLICE_H

namespace grpc_core {

namespace slice_detail {

enum class Storage {
  kStatic,
  kBorrowed,
  kOwned,
  kInlined,
  kUniquelyOwned,
  kUnknown,
};

static inline constexpr bool Reffable(Storage storage) {
  return storage == Storage::kOwned || storage == Storage::kUnknown;
}

static inline constexpr bool Mutable(Storage storage) {
  return storage == Storage::kUniquelyOwned || storage == Storage::kInlined;
}

static inline constexpr bool Owned(Storage storage) {
  return storage == Storage::kOwned || storage == Storage::kUniquelyOwned ||
         storage == Storage::kUnknown;
}

static inline bool CompatibleStorage(grpc_slice_refcount* refcount,
                                     Storage storage) {
  if (refcount == nullptr) {
    switch (storage) {
      case Storage::kInlined:
      case Storage::kUnknown:
      case Storage::kUniquelyOwned:
        return true;
      default:
        return false;
    }
  }
  switch (refcount->GetType()) {
    case grpc_slice_refcount::Type::STATIC:
      switch (storage) {
        case Storage::kStatic:
        case Storage::kBorrowed:
        case Storage::kUnknown:
          return true;
        default:
          return false;
      }
    case grpc_slice_refcount::Type::INTERNED:
      switch (storage) {
        case Storage::kOwned:
        case Storage::kUnknown:
          return true;
        default:
          return false;
      }
    case grpc_slice_refcount::Type::NOP:
      switch (storage) {
        case Storage::kBorrowed:
        case Storage::kUnknown:
          return true;
        default:
          return false;
      }
    case grpc_slice_refcount::Type::REGULAR:
      switch (storage) {
        case Storage::kOwned:
        case Storage::kUnknown:
        case Storage::kUniquelyOwned:
          return true;
        default:
          return false;
      }
  }
}

template <Storage kStorage>
struct IntoMutable;

template <>
struct IntoMutable<Storage::kUniquelyOwned> {
  static constexpr Storage kStorage = Storage::kUniquelyOwned;
  static grpc_slice From(grpc_slice* slice) {
    auto out = *slice;
    *slice = grpc_empty_slice();
    return out;
  }
};

template <>
struct IntoMutable<Storage::kStatic> {
  static constexpr Storage kStorage = Storage::kUniquelyOwned;
  grpc_slice From(grpc_slice* slice) { return grpc_slice_copy(*slice); }
};

template <>
struct IntoMutable<Storage::kUnknown> {
  static constexpr Storage kStorage = Storage::kUniquelyOwned;
  static grpc_slice From(grpc_slice* slice) {
    if (slice->refcount == nullptr) {
      return *slice;
    }
    if (slice->refcount->GetType() == grpc_slice_refcount::Type::REGULAR &&
        slice->refcount->IsRegularUnique()) {
      auto out = *slice;
      *slice = grpc_empty_slice();
      return out;
    }
    return grpc_slice_copy(*slice);
  }
};

template <Storage kStorage>
struct AsOwned;

template <>
struct AsOwned<Storage::kStatic> {
  static constexpr Storage kStorage = Storage::kStatic;
  static grpc_slice From(grpc_slice slice) { return slice; }
};

template <>
struct AsOwned<Storage::kUnknown> {
  static constexpr Storage kStorage = Storage::kUnknown;
  static grpc_slice From(grpc_slice slice) {
    if (slice.refcount == nullptr) {
      return slice;
    }
    switch (slice.refcount->GetType()) {
      case grpc_slice_refcount::Type::STATIC:
        return slice;
      case grpc_slice_refcount::Type::INTERNED:
      case grpc_slice_refcount::Type::REGULAR:
        return grpc_slice_ref_internal(slice);
      case grpc_slice_refcount::Type::NOP:
        return grpc_slice_copy(slice);
    }
  }
};

template <Storage kStorage>
class BasicSlice {
 public:
  BasicSlice(const grpc_slice& slice) : slice_(slice) {
    GPR_ASSERT(CompatibleStorage(this->slice_.refcount, kStorage));
  }

  BasicSlice(const BasicSlice& other) : slice_(other.slice_) {
    static_assert(!Owned(kStorage),
                  "Need to choose Ref() or Copy() for owned slices");
  }
  BasicSlice& operator=(const BasicSlice& other) {
    static_assert(!Owned(kStorage),
                  "Need to choose Ref() or Copy() for owned slices");
    slice_ = other.slice_;
    return *this;
  }

  BasicSlice(BasicSlice&& other);
  BasicSlice& operator=(BasicSlice&& other);

  ~BasicSlice() {
    if (Owned(kStorage)) {
      grpc_slice_unref_internal(slice_);
    }
  }

  BasicSlice Ref() const {
    static_assert(Reffable(kStorage), "Ref() is not supported");
    return BasicSlice(grpc_slice_ref_internal(this->slice_));
  }
  BasicSlice<Storage::kUniquelyOwned> Copy() const {
    return BasicSlice<Storage::kUniquelyOwned>(grpc_slice_copy(this->slice_));
  }
  BasicSlice<IntoMutable<kStorage>::kStorage> IntoMutable() {
    using Impl = ::grpc_core::slice_detail::IntoMutable<kStorage>;
    return BasicSlice<Impl::kStorage>(Impl::From(&this->slice_));
  }
  BasicSlice<AsOwned<kStorage>::kStorage> AsOwned() const {
    using Impl = ::grpc_core::slice_detail::AsOwned<kStorage>;
    return BasicSlice<Impl::kStorage>(Impl::From(&this->slice_));
  }

  operator const BasicSlice<Storage::kUnknown>&() const {
    return *reinterpret_cast<const BasicSlice<Storage::kUnknown>*>(this);
  }

  // Named constructors
  static BasicSlice<Storage::kStatic> FromStaticString(const char* str) {
    static_assert(kStorage == Storage::kStatic,
                  "Static string must be created with static storage");
    return BasicSlice<Storage::kStatic>(grpc_slice_from_static_string(str));
  }

  // Create a new slice with uninitialized contents of some length.
  BasicSlice<Storage::kUniquelyOwned> MakeUninitialized(size_t length) {
    return BasicSlice<Storage::kUniquelyOwned>(grpc_slice_malloc(length));
  }

  // Iterator access to the underlying bytes
  uint8_t* begin() {
    static_assert(Mutable(kStorage),
                  "Slice must be mutable to use mutable begin()");
    return GRPC_SLICE_START_PTR(this->slice_);
  }
  uint8_t* end() {
    static_assert(Mutable(kStorage),
                  "Slice must be mutable to use mutable end()");
    return GRPC_SLICE_END_PTR(this->slice_);
  }
  const uint8_t* begin() const { return GRPC_SLICE_START_PTR(this->slice_); }
  const uint8_t* end() const { return GRPC_SLICE_END_PTR(this->slice_); }
  const uint8_t* cbegin() const { return GRPC_SLICE_START_PTR(this->slice_); }
  const uint8_t* cend() const { return GRPC_SLICE_END_PTR(this->slice_); }

  const grpc_slice& c_slice() { return this->slice_; }

  // As other things... borrowed references.
  absl::string_view as_string_view() const {
    return absl::string_view(reinterpret_cast<const char*>(data()), size());
  }

  // Array access
  uint8_t operator[](size_t i) const {
    return GRPC_SLICE_START_PTR(this->slice_)[i];
  }
  uint8_t& operator[](size_t i) {
    static_assert(Mutable(kStorage),
                  "Slice must be mutable to use mutable operator[]");
    return GRPC_SLICE_START_PTR(this->slice_)[i];
  }

  // Access underlying data
  uint8_t* data() {
    static_assert(Mutable(kStorage),
                  "Slice must be mutable to use mutable data()");
    return GRPC_SLICE_START_PTR(this->slice_);
  }
  const uint8_t* data() const { return GRPC_SLICE_START_PTR(this->slice_); }

  // Size of the slice
  size_t size() const { return GRPC_SLICE_LENGTH(this->slice_); }
  size_t length() const { return size(); }
  bool empty() const { return size() == 0; }

  // Comparisons
  template <Storage kOtherStorage>
  bool operator==(const BasicSlice<kOtherStorage>& rhs) const {
    return grpc_slice_eq(c_slice(), rhs.c_slice());
  }
  bool operator==(absl::string_view rhs) const {
    return as_string_view() == rhs;
  }
  template <Storage kOtherStorage>
  bool operator<(const BasicSlice<kOtherStorage>& rhs) const {
    return grpc_slice_cmp(c_slice(), rhs.c_slice()) < 0;
  }
  template <Storage kOtherStorage>
  bool operator>(const BasicSlice<kOtherStorage>& rhs) const {
    return grpc_slice_cmp(c_slice(), rhs.c_slice()) > 0;
  }

  // Does this slice point to the same object with the same length?
  template <Storage kOtherStorage>
  bool is_equivalent_to(const BasicSlice<kOtherStorage>& other) const {
    return grpc_slice_is_equivalent(c_slice(), other.c_slice()) != 0;
  }

 private:
  grpc_slice slice_;
};

}  // namespace slice_detail

using Slice = slice_detail::BasicSlice<slice_detail::Storage::kUnknown>;
using StaticSlice = slice_detail::BasicSlice<slice_detail::Storage::kStatic>;
using ExternalSlice =
    slice_detail::BasicSlice<slice_detail::Storage::kBorrowed>;

}  // namespace grpc_core

#endif
