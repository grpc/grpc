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

static constexpr bool Reffable(Storage storage) {
  return storage == Storage::kOwned || storage == Storage::kUnknown;
}

static constexpr bool Mutable(Storage storage) {
  return storage == Storage::kUniquelyOwned || storage == Storage::kInlined;
}

template <Storage kStorage>
struct IntoMutable;

template <bool kOwned>
class UnderlyingSlice {
 protected:
  ~UnderlyingSlice() = default;

 private:
  grpc_slice slice_;
};

template <>
class UnderlyingSlice<true> {
 public:
  UnderlyingSlice(const UnderlyingSlice&) = delete;
  UnderlyingSlice& operator=(const UnderlyingSlice&) = delete;

  const grpc_slice& c_slice() { return slice_; }

 protected:
  ~UnderlyingSlice() { grpc_slice_unref_internal(slice_); }

 private:
  grpc_slice slice_;
};

template <Storage kStorage>
class BasicSlice : public UnderlyingSlice<kStorage == Storage::kOwned> {
 public:
  absl::enable_if_t<Reffable(kStorage), BasicSlice> Ref() const {
    return BasicSlice(grpc_slice_ref_internal(slice_));
  }
  BasicSlice<Storage::kUniquelyOwned> Copy() const {
    return BasicSlice<Storage::kUniquelyOwned>(grpc_slice_copy(slice_));
  }
  BasicSlice<IntoMutable<kStorage>::kStorage> IntoMutable() {
    return BasicSlice<IntoMutable<kStorage>::kStorage>(
        IntoMutable<kStorage>::From(c_slice()));
  }

  operator const BasicSlice<Storage::kUnknown>&() const {
    return *reinterpret_cast<const BasicSlice<Storage::kUnknown>*>(this);
  }

  // Named constructors
  static BasicSlice FromC(const grpc_slice& slice) {
    GPR_ASSERT(CompatibleStorage(slice.refcount, kStorage));
    return Slice(slice);
  }
  static absl::enable_if_t<
      kStorage == Storage::kStatic || kStorage == Storage::kUnknown, BasicSlice>
  FromStaticString(const char* str) {
    return FromC(grpc_slice_from_static_string(str));
  }

  // Create a new slice with uninitialized contents of some length.
  BasicSlice<Storage::kUniquelyOwned> MakeUninitialized(size_t length) {
    return BasicSlice<Storage::kUniquelyOwned>::FromC(
        grpc_slice_malloc(length));
  }

  // Iterator access to the underlying bytes
  absl::enable_if_t<Mutable<kStorage>, uint8_t*> begin() {
    return GRPC_SLICE_START_PTR(slice_);
  }
  absl::enable_if_t<Mutable<kStorage>, uint8_t*> end() {
    return GRPC_SLICE_END_PTR(slice_);
  }
  const uint8_t* begin() const { return GRPC_SLICE_START_PTR(slice_); }
  const uint8_t* end() const { return GRPC_SLICE_END_PTR(slice_); }
  const uint8_t* cbegin() const { return GRPC_SLICE_START_PTR(slice_); }
  const uint8_t* cend() const { return GRPC_SLICE_END_PTR(slice_); }

  // Array access
  uint8_t operator[](size_t i) const { return GRPC_SLICE_START_PTR(slice_)[i]; }
  absl::enable_if_t<Mutable<kStorage>, uint8_t&> operator[](size_t i) {
    return GRPC_SLICE_START_PTR(slice_)[i];
  }

  // Access underlying data
  absl::enable_if_t<Mutable<kStorage>, uint8_t*> data() {
    return GRPC_SLICE_START_PTR(slice_);
  }
  const uint8_t* data() const { return GRPC_SLICE_START_PTR(slice_); }

  // Size of the slice
  size_t size() const { return GRPC_SLICE_LENGTH(slice_); }
  size_t length() const { return size(); }
  bool empty() const { return size() == 0; }

  // Comparisons
  template <Storage kOtherStorage>
  bool operator==(const BasicSlice<kOtherStorage>& rhs) const {
    return grpc_slice_eq(c_slice(), rhs.c_slice());
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
    return grpc_slice_is_equivalent(slice_, other.slice_) != 0;
  }
};

}  // namespace slice_detail

using Slice = slice_detail::BasicSlice<slice_detail::Storage::kUnknown>;
using StaticSlice = slice_detail::BasicSlice<slice_detail::Storage::kStatic>;
using ExternalSlice =
    slice_detail::BasicSlice<slice_detail::Storage::kBorrowed>;

}  // namespace grpc_core

#endif
