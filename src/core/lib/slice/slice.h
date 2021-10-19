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

class Slice {
 public:
  Slice() : Slice(grpc_empty_slice()) {}
  Slice(const Slice&) = delete;
  Slice& operator=(const Slice&) = delete;
  Slice(Slice&& other) { other.Swap(this); }
  Slice& operator=(Slice&& other) {
    other.Swap(this);
    return *this;
  }

  Slice Ref() const { return Slice(grpc_slice_ref_internal(slice_)); }
  Slice Copy() const { return Slice(grpc_slice_copy(slice_)); }

  // Named constructors
  static Slice FromC(const grpc_slice& slice) { return Slice(slice); }

  // Iterator access to the underlying bytes
  uint8_t* begin() { return GRPC_SLICE_START_PTR(slice_); }
  uint8_t* end() { return GRPC_SLICE_END_PTR(slice_); }
  const uint8_t* begin() const { return GRPC_SLICE_START_PTR(slice_); }
  const uint8_t* end() const { return GRPC_SLICE_END_PTR(slice_); }
  const uint8_t* cbegin() const { return GRPC_SLICE_START_PTR(slice_); }
  const uint8_t* cend() const { return GRPC_SLICE_END_PTR(slice_); }

  // Array access
  uint8_t operator[](size_t i) const { return GRPC_SLICE_START_PTR(slice_)[i]; }
  uint8_t& operator[](size_t i) { return GRPC_SLICE_START_PTR(slice_)[i]; }

  // Borrow the underlying object.
  const grpc_slice& c_slice() { return slice_; }

  // Access underlying data
  uint8_t* data() { return GRPC_SLICE_START_PTR(slice_); }
  const uint8_t* data() const { return GRPC_SLICE_START_PTR(slice_); }

  // Size of the slice
  size_t size() const { return GRPC_SLICE_LENGTH(slice_); }
  size_t length() const { return size(); }
  bool empty() const { return size() == 0; }

  // Comparisons
  bool operator==(const Slice& rhs) const {
    return grpc_slice_eq(slice_, rhs.slice_);
  }
  bool operator<(const Slice& rhs) const {
    return grpc_slice_cmp(slice_, rhs.slice_) < 0;
  }
  bool operator>(const Slice& rhs) const {
    return grpc_slice_cmp(slice_, rhs.slice_) > 0;
  }

  // Does this slice point to the same object with the same length?
  bool is_equivalent_to(const Slice& other) const {
    return grpc_slice_is_equivalent(slice_, other.slice_) != 0;
  }

 protected:
  void SwapUnderlying(Slice* slice) { std::swap(slice_, slice->slice_); }
  explicit Slice(grpc_slice slice) : slice_(slice) {}

 private:
  grpc_slice slice_;
};

}  // namespace grpc_core

#endif
