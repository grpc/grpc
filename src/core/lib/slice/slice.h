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

static constexpr grpc_slice EmptySlice() { return {nullptr, {}}; }

enum class Storage {
  kStatic,
  kBorrowed,
  kOwned,
  kUniquelyOwned,
  kUnknown,
};

static inline constexpr bool Mutable(Storage storage) {
  return storage == Storage::kUniquelyOwned;
}

static inline constexpr bool Owned(Storage storage) {
  return storage == Storage::kOwned || storage == Storage::kUniquelyOwned ||
         storage == Storage::kUnknown;
}

static inline constexpr bool HasCopyConstructors(Storage storage) {
  return storage == Storage::kOwned || storage == Storage::kUniquelyOwned ||
         storage == Storage::kUnknown;
}

static inline bool CompatibleStorage(grpc_slice_refcount* refcount,
                                     Storage storage) {
  if (refcount == nullptr) {
    switch (storage) {
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

// BaseSlice holds the grpc_slice object, but does not apply refcounting policy.
// It does export immutable access into the slice, such that this can be shared
// by all storage policies.
class BaseSlice {
 public:
  // Iterator access to the underlying bytes
  const uint8_t* begin() const { return GRPC_SLICE_START_PTR(this->slice_); }
  const uint8_t* end() const { return GRPC_SLICE_END_PTR(this->slice_); }
  const uint8_t* cbegin() const { return GRPC_SLICE_START_PTR(this->slice_); }
  const uint8_t* cend() const { return GRPC_SLICE_END_PTR(this->slice_); }

  const grpc_slice& c_slice() const { return this->slice_; }

  grpc_slice TakeCSlice() {
    grpc_slice out = this->slice_;
    this->slice_ = EmptySlice();
    return out;
  }

  // As other things... borrowed references.
  absl::string_view as_string_view() const {
    return absl::string_view(reinterpret_cast<const char*>(data()), size());
  }

  // Array access
  uint8_t operator[](size_t i) const {
    return GRPC_SLICE_START_PTR(this->slice_)[i];
  }

  // Access underlying data
  const uint8_t* data() const { return GRPC_SLICE_START_PTR(this->slice_); }

  // Size of the slice
  size_t size() const { return GRPC_SLICE_LENGTH(this->slice_); }
  size_t length() const { return size(); }
  bool empty() const { return size() == 0; }

  bool is_equivalent(const BaseSlice& other) const {
    return grpc_slice_is_equivalent(slice_, other.slice_);
  }

 protected:
  BaseSlice() : slice_(EmptySlice()) {}
  BaseSlice(const grpc_slice& slice) : slice_(slice) {}
  ~BaseSlice() = default;
  grpc_slice slice_;
};

inline bool operator==(const BaseSlice& a, const BaseSlice& b) {
  return grpc_slice_eq(a.c_slice(), b.c_slice()) != 0;
}

inline bool operator==(const BaseSlice& a, absl::string_view b) {
  return a.as_string_view() == b;
}

inline bool operator==(absl::string_view a, const BaseSlice& b) {
  return a == b.as_string_view();
}

inline bool operator==(const BaseSlice& a, const grpc_slice& b) {
  return grpc_slice_eq(a.c_slice(), b) != 0;
}

inline bool operator==(const grpc_slice& a, const BaseSlice& b) {
  return grpc_slice_eq(a, b.c_slice()) != 0;
}

template <bool kOwned, class Base>
class CopyPolicy;

template <class Base>
class CopyPolicy<true, Base> : public Base {
 public:
  CopyPolicy(const CopyPolicy&) = delete;
  CopyPolicy& operator=(const CopyPolicy&) = delete;
  CopyPolicy(CopyPolicy&& other) : Base(other.slice_) {
    other.slice_ = EmptySlice();
  }
  CopyPolicy& operator=(CopyPolicy&& other) {
    std::swap(this->slice_, other.slice_);
    return *this;
  }

 protected:
  CopyPolicy() = default;
  CopyPolicy(grpc_slice slice) : Base(slice) {}
  ~CopyPolicy() { grpc_slice_unref_internal(this->slice_); }
};

template <class Base>
class CopyPolicy<false, Base> : public Base {
 public:
  CopyPolicy(const CopyPolicy&) = default;
  CopyPolicy& operator=(const CopyPolicy&) = default;

 protected:
  CopyPolicy() = default;
  CopyPolicy(grpc_slice slice) : Base(slice) {}
  ~CopyPolicy() = default;
};

template <Storage kStorage>
class SliceImpl;

template <Storage kStorage, class Base>
class CommonMethods : public Base {
 public:
  SliceImpl<Storage::kUnknown> AsOwned() const;
  SliceImpl<Storage::kUnknown> IntoOwned() const;

 protected:
  CommonMethods() = default;
  CommonMethods(const grpc_slice& slice) : Base(slice) {}
  ~CommonMethods() = default;
};

template <Storage kStorage, class Base>
class RefMethod : public Base {
 protected:
  RefMethod() = default;
  RefMethod(const grpc_slice& slice) : Base(slice) {}
  ~RefMethod() = default;
};

template <class Base>
class RefMethod<Storage::kOwned, Base> : public Base {
 public:
  SliceImpl<Storage::kOwned> Ref() const;

 protected:
  RefMethod() = default;
  RefMethod(const grpc_slice& slice) : Base(slice) {}
  ~RefMethod() = default;
};

template <class Base>
class RefMethod<Storage::kUnknown, Base> : public Base {
 public:
  SliceImpl<Storage::kUnknown> Ref() const;

 protected:
  RefMethod() = default;
  RefMethod(const grpc_slice& slice) : Base(slice) {}
  ~RefMethod() = default;
};

template <bool kHasCopyConstructors>
struct CopyConstructors {};

template <>
struct CopyConstructors<true> {
  static SliceImpl<Storage::kUniquelyOwned> FromCopiedString(const char* name);
};

template <Storage kStorage>
using BasicSlice =
    CopyPolicy<Owned(kStorage),
               RefMethod<kStorage, CommonMethods<kStorage, BaseSlice>>>;

template <Storage kStorage>
using NamedConstructors = CopyConstructors<HasCopyConstructors(kStorage)>;

template <Storage kStorage>
class SliceImpl : public BasicSlice<kStorage>,
                  public NamedConstructors<kStorage> {
 public:
  SliceImpl() = default;
  explicit SliceImpl(const grpc_slice& slice) : BasicSlice<kStorage>(slice) {
    GPR_ASSERT(CompatibleStorage(this->slice_.refcount, kStorage));
  }
};

template <>
class SliceImpl<Storage::kUnknown>
    : public BasicSlice<Storage::kUnknown>,
      public NamedConstructors<Storage::kUnknown> {
 public:
  SliceImpl() = default;
  explicit SliceImpl(const grpc_slice& slice)
      : BasicSlice<Storage::kUnknown>(slice) {}
  template <Storage kStorage>
  SliceImpl(SliceImpl<kStorage>&& other)
      : BasicSlice<Storage::kUnknown>(other.TakeCSlice()) {}

  static SliceImpl FromRefcountAndBytes(grpc_slice_refcount* r,
                                        const uint8_t* begin,
                                        const uint8_t* end) {
    grpc_slice out;
    out.refcount = r;
    r->Ref();
    out.data.refcounted.bytes = const_cast<uint8_t*>(begin);
    out.data.refcounted.length = end - begin;
    return SliceImpl(out);
  }
};

template <>
class SliceImpl<Storage::kStatic> : public BasicSlice<Storage::kStatic>,
                                    public NamedConstructors<Storage::kStatic> {
 public:
  SliceImpl() = default;
  explicit SliceImpl(const grpc_slice& slice)
      : BasicSlice<Storage::kStatic>(slice) {}
  SliceImpl(const StaticMetadataSlice& slice)
      : BasicSlice<Storage::kStatic>(slice) {}
};

template <class Base>
SliceImpl<Storage::kOwned> RefMethod<Storage::kOwned, Base>::Ref() const {
  this->slice_.refcount->Ref();
  return SliceImpl<Storage::kOwned>(this->slice_);
}

}  // namespace slice_detail

using Slice = slice_detail::SliceImpl<slice_detail::Storage::kUnknown>;
using StaticSlice = slice_detail::SliceImpl<slice_detail::Storage::kStatic>;
using ExternalSlice = slice_detail::SliceImpl<slice_detail::Storage::kBorrowed>;

}  // namespace grpc_core

#endif
