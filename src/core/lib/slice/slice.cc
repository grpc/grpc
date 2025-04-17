//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/core/lib/slice/slice.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <new>

#include "absl/log/check.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/util/memory.h"

char* grpc_slice_to_c_string(grpc_slice slice) {
  char* out = static_cast<char*>(gpr_malloc(GRPC_SLICE_LENGTH(slice) + 1));
  memcpy(out, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));
  out[GRPC_SLICE_LENGTH(slice)] = 0;
  return out;
}

grpc_slice grpc_empty_slice(void) {
  return grpc_core::slice_detail::EmptySlice();
}

grpc_slice grpc_slice_copy(grpc_slice s) {
  grpc_slice out = GRPC_SLICE_MALLOC(GRPC_SLICE_LENGTH(s));
  memcpy(GRPC_SLICE_START_PTR(out), GRPC_SLICE_START_PTR(s),
         GRPC_SLICE_LENGTH(s));
  return out;
}

namespace grpc_core {

// grpc_slice_new support structures - we create a refcount object extended
// with the user provided data pointer & destroy function
class NewSliceRefcount : public grpc_slice_refcount {
 public:
  NewSliceRefcount(void (*destroy)(void*), void* user_data)
      : grpc_slice_refcount(Destroy),
        user_destroy_(destroy),
        user_data_(user_data) {}
  ~NewSliceRefcount() { user_destroy_(user_data_); }

 private:
  static void Destroy(grpc_slice_refcount* arg) {
    delete static_cast<NewSliceRefcount*>(arg);
  }

  void (*user_destroy_)(void*);
  void* user_data_;
};

}  // namespace grpc_core

size_t grpc_slice_memory_usage(grpc_slice s) {
  if (s.refcount == nullptr ||
      s.refcount == grpc_slice_refcount::NoopRefcount()) {
    return 0;
  } else {
    return s.data.refcounted.length;
  }
}

grpc_slice grpc_slice_from_static_buffer(const void* s, size_t len) {
  return grpc_core::StaticSlice::FromStaticBuffer(s, len).TakeCSlice();
}

grpc_slice grpc_slice_from_static_string(const char* s) {
  return grpc_core::StaticSlice::FromStaticString(s).TakeCSlice();
}

grpc_slice grpc_slice_new_with_user_data(void* p, size_t len,
                                         void (*destroy)(void*),
                                         void* user_data) {
  grpc_slice slice;
  slice.refcount = new grpc_core::NewSliceRefcount(destroy, user_data);
  slice.data.refcounted.bytes = static_cast<uint8_t*>(p);
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_new(void* p, size_t len, void (*destroy)(void*)) {
  // Pass "p" to *destroy when the slice is no longer needed.
  return grpc_slice_new_with_user_data(p, len, destroy, p);
}

namespace grpc_core {
// grpc_slice_new_with_len support structures - we create a refcount object
// extended with the user provided data pointer & destroy function
class NewWithLenSliceRefcount : public grpc_slice_refcount {
 public:
  NewWithLenSliceRefcount(void (*destroy)(void*, size_t), void* user_data,
                          size_t user_length)
      : grpc_slice_refcount(Destroy),
        user_data_(user_data),
        user_length_(user_length),
        user_destroy_(destroy) {}
  ~NewWithLenSliceRefcount() { user_destroy_(user_data_, user_length_); }

 private:
  static void Destroy(grpc_slice_refcount* arg) {
    delete static_cast<NewWithLenSliceRefcount*>(arg);
  }

  void* user_data_;
  size_t user_length_;
  void (*user_destroy_)(void*, size_t);
};

/// grpc_slice_from_moved_(string|buffer) ref count .
class MovedStringSliceRefCount : public grpc_slice_refcount {
 public:
  explicit MovedStringSliceRefCount(UniquePtr<char>&& str)
      : grpc_slice_refcount(Destroy), str_(std::move(str)) {}

 private:
  static void Destroy(grpc_slice_refcount* arg) {
    delete static_cast<MovedStringSliceRefCount*>(arg);
  }

  UniquePtr<char> str_;
};

// grpc_slice_from_cpp_string() ref count.
class MovedCppStringSliceRefCount : public grpc_slice_refcount {
 public:
  explicit MovedCppStringSliceRefCount(std::string&& str)
      : grpc_slice_refcount(Destroy), str_(std::move(str)) {}

  uint8_t* data() {
    return reinterpret_cast<uint8_t*>(const_cast<char*>(str_.data()));
  }

  size_t size() const { return str_.size(); }

 private:
  static void Destroy(grpc_slice_refcount* arg) {
    delete static_cast<MovedCppStringSliceRefCount*>(arg);
  }

  std::string str_;
};

}  // namespace grpc_core

grpc_slice grpc_slice_new_with_len(void* p, size_t len,
                                   void (*destroy)(void*, size_t)) {
  grpc_slice slice;
  slice.refcount = new grpc_core::NewWithLenSliceRefcount(destroy, p, len);
  slice.data.refcounted.bytes = static_cast<uint8_t*>(p);
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_from_copied_buffer(const char* source, size_t len) {
  if (len == 0) return grpc_empty_slice();
  grpc_slice out = grpc_slice_malloc(len);
  memcpy(GRPC_SLICE_START_PTR(out), source, len);
  return out;
}

grpc_slice grpc_slice_from_copied_string(const char* source) {
  return grpc_slice_from_copied_buffer(source, strlen(source));
}

grpc_slice grpc_slice_from_moved_buffer(grpc_core::UniquePtr<char> p,
                                        size_t len) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(p.get());
  grpc_slice slice;
  if (len <= sizeof(slice.data.inlined.bytes)) {
    slice.refcount = nullptr;
    slice.data.inlined.length = len;
    memcpy(GRPC_SLICE_START_PTR(slice), ptr, len);
  } else {
    slice.refcount = new grpc_core::MovedStringSliceRefCount(std::move(p));
    slice.data.refcounted.bytes = ptr;
    slice.data.refcounted.length = len;
  }
  return slice;
}

grpc_slice grpc_slice_from_moved_string(grpc_core::UniquePtr<char> p) {
  const size_t len = strlen(p.get());
  return grpc_slice_from_moved_buffer(std::move(p), len);
}

grpc_slice grpc_slice_from_cpp_string(std::string str) {
  grpc_slice slice;
  if (str.size() <= sizeof(slice.data.inlined.bytes)) {
    slice.refcount = nullptr;
    slice.data.inlined.length = str.size();
    memcpy(GRPC_SLICE_START_PTR(slice), str.data(), str.size());
  } else {
    auto* refcount = new grpc_core::MovedCppStringSliceRefCount(std::move(str));
    slice.data.refcounted.bytes = refcount->data();
    slice.data.refcounted.length = refcount->size();
    slice.refcount = refcount;
  }
  return slice;
}

grpc_slice grpc_slice_malloc_large(size_t length) {
  grpc_slice slice;
  uint8_t* memory = new uint8_t[sizeof(grpc_slice_refcount) + length];
  slice.refcount = new (memory) grpc_slice_refcount(
      [](grpc_slice_refcount* p) { delete[] reinterpret_cast<uint8_t*>(p); });
  slice.data.refcounted.bytes = memory + sizeof(grpc_slice_refcount);
  slice.data.refcounted.length = length;
  return slice;
}

grpc_slice grpc_slice_malloc(size_t length) {
  if (length <= GRPC_SLICE_INLINED_SIZE) {
    grpc_slice slice;
    slice.refcount = nullptr;
    slice.data.inlined.length = length;
    return slice;
  } else {
    return grpc_slice_malloc_large(length);
  }
}

static grpc_slice sub_no_ref(const grpc_slice& source, size_t begin,
                             size_t end) {
  grpc_slice subset;

  CHECK(end >= begin);

  if (source.refcount != nullptr) {
    // Enforce preconditions
    CHECK(source.data.refcounted.length >= end);

    // Build the result
    subset.refcount = source.refcount;
    // Point into the source array
    subset.data.refcounted.bytes = source.data.refcounted.bytes + begin;
    subset.data.refcounted.length = end - begin;
  } else {
    // Enforce preconditions
    CHECK(source.data.inlined.length >= end);
    subset.refcount = nullptr;
    subset.data.inlined.length = static_cast<uint8_t>(end - begin);
    memcpy(subset.data.inlined.bytes, source.data.inlined.bytes + begin,
           end - begin);
  }
  return subset;
}

grpc_slice grpc_slice_sub_no_ref(grpc_slice source, size_t begin, size_t end) {
  return sub_no_ref(source, begin, end);
}

grpc_slice grpc_slice_sub(grpc_slice source, size_t begin, size_t end) {
  grpc_slice subset;

  if (end - begin <= sizeof(subset.data.inlined.bytes)) {
    subset.refcount = nullptr;
    subset.data.inlined.length = static_cast<uint8_t>(end - begin);
    memcpy(subset.data.inlined.bytes, GRPC_SLICE_START_PTR(source) + begin,
           end - begin);
  } else {
    subset = grpc_slice_sub_no_ref(source, begin, end);
    // Bump the refcount
    if (subset.refcount != grpc_slice_refcount::NoopRefcount()) {
      subset.refcount->Ref({});
    }
  }
  return subset;
}

template <bool allow_inline>
grpc_slice grpc_slice_split_tail_maybe_ref_impl(grpc_slice* source,
                                                size_t split,
                                                grpc_slice_ref_whom ref_whom) {
  grpc_slice tail;

  if (source->refcount == nullptr) {
    // inlined data, copy it out
    CHECK(source->data.inlined.length >= split);
    tail.refcount = nullptr;
    tail.data.inlined.length =
        static_cast<uint8_t>(source->data.inlined.length - split);
    memcpy(tail.data.inlined.bytes, source->data.inlined.bytes + split,
           tail.data.inlined.length);
    source->data.inlined.length = static_cast<uint8_t>(split);
  } else if (source->refcount == grpc_slice_refcount::NoopRefcount()) {
    // refcount == NoopRefcount(), so we can just split in-place
    tail.refcount = grpc_slice_refcount::NoopRefcount();
    tail.data.refcounted.bytes = source->data.refcounted.bytes + split;
    tail.data.refcounted.length = source->data.refcounted.length - split;
    source->data.refcounted.length = split;
  } else {
    size_t tail_length = source->data.refcounted.length - split;
    CHECK(source->data.refcounted.length >= split);
    if (allow_inline && tail_length < sizeof(tail.data.inlined.bytes) &&
        ref_whom != GRPC_SLICE_REF_TAIL) {
      // Copy out the bytes - it'll be cheaper than refcounting
      tail.refcount = nullptr;
      tail.data.inlined.length = static_cast<uint8_t>(tail_length);
      memcpy(tail.data.inlined.bytes, source->data.refcounted.bytes + split,
             tail_length);
    } else {
      // Build the result
      switch (ref_whom) {
        case GRPC_SLICE_REF_TAIL:
          tail.refcount = source->refcount;
          source->refcount = grpc_slice_refcount::NoopRefcount();
          break;
        case GRPC_SLICE_REF_HEAD:
          tail.refcount = grpc_slice_refcount::NoopRefcount();
          break;
        case GRPC_SLICE_REF_BOTH:
          tail.refcount = source->refcount;
          // Bump the refcount
          if (tail.refcount != grpc_slice_refcount::NoopRefcount()) {
            tail.refcount->Ref({});
          }
          break;
      }
      // Point into the source array
      tail.data.refcounted.bytes = source->data.refcounted.bytes + split;
      tail.data.refcounted.length = tail_length;
    }
    source->data.refcounted.length = split;
  }

  return tail;
}

grpc_slice grpc_slice_split_tail_maybe_ref(grpc_slice* source, size_t split,
                                           grpc_slice_ref_whom ref_whom) {
  return grpc_slice_split_tail_maybe_ref_impl<true>(source, split, ref_whom);
}

grpc_slice grpc_slice_split_tail_maybe_ref_no_inline(
    grpc_slice* source, size_t split, grpc_slice_ref_whom ref_whom) {
  return grpc_slice_split_tail_maybe_ref_impl<false>(source, split, ref_whom);
}

grpc_slice grpc_slice_split_tail(grpc_slice* source, size_t split) {
  return grpc_slice_split_tail_maybe_ref(source, split, GRPC_SLICE_REF_BOTH);
}

grpc_slice grpc_slice_split_tail_no_inline(grpc_slice* source, size_t split) {
  return grpc_slice_split_tail_maybe_ref_no_inline(source, split,
                                                   GRPC_SLICE_REF_BOTH);
}

template <bool allow_inline>
grpc_slice grpc_slice_split_head_impl(grpc_slice* source, size_t split) {
  grpc_slice head;

  if (source->refcount == nullptr) {
    CHECK(source->data.inlined.length >= split);

    head.refcount = nullptr;
    head.data.inlined.length = static_cast<uint8_t>(split);
    memcpy(head.data.inlined.bytes, source->data.inlined.bytes, split);
    source->data.inlined.length =
        static_cast<uint8_t>(source->data.inlined.length - split);
    memmove(source->data.inlined.bytes, source->data.inlined.bytes + split,
            source->data.inlined.length);
  } else if (allow_inline && split < sizeof(head.data.inlined.bytes)) {
    CHECK(source->data.refcounted.length >= split);

    head.refcount = nullptr;
    head.data.inlined.length = static_cast<uint8_t>(split);
    memcpy(head.data.inlined.bytes, source->data.refcounted.bytes, split);
    source->data.refcounted.bytes += split;
    source->data.refcounted.length -= split;
  } else {
    CHECK(source->data.refcounted.length >= split);

    // Build the result
    head.refcount = source->refcount;
    // Bump the refcount
    if (head.refcount != grpc_slice_refcount::NoopRefcount()) {
      head.refcount->Ref({});
    }
    // Point into the source array
    head.data.refcounted.bytes = source->data.refcounted.bytes;
    head.data.refcounted.length = split;
    source->data.refcounted.bytes += split;
    source->data.refcounted.length -= split;
  }

  return head;
}

grpc_slice grpc_slice_split_head(grpc_slice* source, size_t split) {
  return grpc_slice_split_head_impl<true>(source, split);
}

grpc_slice grpc_slice_split_head_no_inline(grpc_slice* source, size_t split) {
  return grpc_slice_split_head_impl<false>(source, split);
}

int grpc_slice_eq(grpc_slice a, grpc_slice b) {
  if (GRPC_SLICE_LENGTH(a) != GRPC_SLICE_LENGTH(b)) return false;
  if (GRPC_SLICE_LENGTH(a) == 0) return true;
  return 0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                     GRPC_SLICE_LENGTH(a));
}

int grpc_slice_differs_refcounted(const grpc_slice& a,
                                  const grpc_slice& b_not_inline) {
  size_t a_len;
  const uint8_t* a_ptr;
  if (a.refcount) {
    a_len = a.data.refcounted.length;
    a_ptr = a.data.refcounted.bytes;
  } else {
    a_len = a.data.inlined.length;
    a_ptr = &a.data.inlined.bytes[0];
  }
  if (a_len != b_not_inline.data.refcounted.length) {
    return true;
  }
  if (a_len == 0) {
    return false;
  }
  // This check *must* occur after the a_len == 0 check
  // to retain compatibility with grpc_slice_eq.
  if (a_ptr == nullptr) {
    return true;
  }
  return memcmp(a_ptr, b_not_inline.data.refcounted.bytes, a_len);
}

int grpc_slice_cmp(grpc_slice a, grpc_slice b) {
  int d = static_cast<int>(GRPC_SLICE_LENGTH(a) - GRPC_SLICE_LENGTH(b));
  if (d != 0) return d;
  return memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                GRPC_SLICE_LENGTH(a));
}

int grpc_slice_str_cmp(grpc_slice a, const char* b) {
  size_t b_length = strlen(b);
  int d = static_cast<int>(GRPC_SLICE_LENGTH(a) - b_length);
  if (d != 0) return d;
  return memcmp(GRPC_SLICE_START_PTR(a), b, b_length);
}

int grpc_slice_is_equivalent(grpc_slice a, grpc_slice b) {
  if (a.refcount == nullptr || b.refcount == nullptr) {
    return grpc_slice_eq(a, b);
  }
  return a.data.refcounted.length == b.data.refcounted.length &&
         a.data.refcounted.bytes == b.data.refcounted.bytes;
}

int grpc_slice_buf_start_eq(grpc_slice a, const void* b, size_t len) {
  if (GRPC_SLICE_LENGTH(a) < len) return 0;
  return 0 == memcmp(GRPC_SLICE_START_PTR(a), b, len);
}

int grpc_slice_rchr(grpc_slice s, char c) {
  const char* b = reinterpret_cast<const char*> GRPC_SLICE_START_PTR(s);
  int i;
  for (i = static_cast<int> GRPC_SLICE_LENGTH(s) - 1; i != -1 && b[i] != c;
       i--) {
  }
  return i;
}

int grpc_slice_chr(grpc_slice s, char c) {
  const char* b = reinterpret_cast<const char*> GRPC_SLICE_START_PTR(s);
  const char* p = static_cast<const char*>(memchr(b, c, GRPC_SLICE_LENGTH(s)));
  return p == nullptr ? -1 : static_cast<int>(p - b);
}

int grpc_slice_slice(grpc_slice haystack, grpc_slice needle) {
  size_t haystack_len = GRPC_SLICE_LENGTH(haystack);
  const uint8_t* haystack_bytes = GRPC_SLICE_START_PTR(haystack);
  size_t needle_len = GRPC_SLICE_LENGTH(needle);
  const uint8_t* needle_bytes = GRPC_SLICE_START_PTR(needle);

  if (haystack_len == 0 || needle_len == 0) return -1;
  if (haystack_len < needle_len) return -1;
  if (haystack_len == needle_len) {
    return grpc_slice_eq(haystack, needle) ? 0 : -1;
  }
  if (needle_len == 1) {
    return grpc_slice_chr(haystack, static_cast<char>(*needle_bytes));
  }

  const uint8_t* last = haystack_bytes + haystack_len - needle_len;
  for (const uint8_t* cur = haystack_bytes; cur <= last; ++cur) {
    if (0 == memcmp(cur, needle_bytes, needle_len)) {
      return static_cast<int>(cur - haystack_bytes);
    }
  }
  return -1;
}

grpc_slice grpc_slice_dup(grpc_slice a) {
  grpc_slice copy = GRPC_SLICE_MALLOC(GRPC_SLICE_LENGTH(a));
  memcpy(GRPC_SLICE_START_PTR(copy), GRPC_SLICE_START_PTR(a),
         GRPC_SLICE_LENGTH(a));
  return copy;
}

grpc_slice grpc_slice_ref(grpc_slice slice) {
  return grpc_core::CSliceRef(slice);
}

void grpc_slice_unref(grpc_slice slice) { grpc_core::CSliceUnref(slice); }
