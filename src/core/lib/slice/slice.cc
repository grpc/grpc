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

#include <grpc/support/port_platform.h>

#include "src/core/lib/slice/slice_internal.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <string.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/exec_ctx.h"

char* grpc_slice_to_c_string(grpc_slice slice) {
  char* out = static_cast<char*>(gpr_malloc(GRPC_SLICE_LENGTH(slice) + 1));
  memcpy(out, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));
  out[GRPC_SLICE_LENGTH(slice)] = 0;
  return out;
}

grpc_slice grpc_empty_slice(void) {
  grpc_slice out;
  out.refcount = nullptr;
  out.data.inlined.length = 0;
  return out;
}

grpc_slice grpc_slice_copy(grpc_slice s) {
  grpc_slice out = GRPC_SLICE_MALLOC(GRPC_SLICE_LENGTH(s));
  memcpy(GRPC_SLICE_START_PTR(out), GRPC_SLICE_START_PTR(s),
         GRPC_SLICE_LENGTH(s));
  return out;
}

/* Public API */
grpc_slice grpc_slice_ref(grpc_slice slice) {
  return grpc_slice_ref_internal(slice);
}

/* Public API */
void grpc_slice_unref(grpc_slice slice) {
  if (grpc_core::ExecCtx::Get() == nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_slice_unref_internal(slice);
  } else {
    grpc_slice_unref_internal(slice);
  }
}

/* grpc_slice_from_static_string support structure - a refcount that does
   nothing */
static grpc_slice_refcount NoopRefcount =
    grpc_slice_refcount(grpc_slice_refcount::Type::NOP);

size_t grpc_slice_memory_usage(grpc_slice s) {
  if (s.refcount == nullptr || s.refcount == &NoopRefcount) {
    return 0;
  } else {
    return s.data.refcounted.length;
  }
}

grpc_slice grpc_slice_from_static_buffer(const void* s, size_t len) {
  grpc_slice slice;
  slice.refcount = &NoopRefcount;
  slice.data.refcounted.bytes = (uint8_t*)s;
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_from_static_string(const char* s) {
  return grpc_slice_from_static_buffer(s, strlen(s));
}

namespace grpc_core {

/* grpc_slice_new support structures - we create a refcount object extended
   with the user provided data pointer & destroy function */
class NewSliceRefcount {
 public:
  static void Destroy(void* arg) {
    Delete(static_cast<NewSliceRefcount*>(arg));
  }

  NewSliceRefcount(void (*destroy)(void*), void* user_data)
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_),
        user_destroy_(destroy),
        user_data_(user_data) {}

  GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  ~NewSliceRefcount() { user_destroy_(user_data_); }

  grpc_slice_refcount base_;
  RefCount refs_;
  void (*user_destroy_)(void*);
  void* user_data_;
};

}  // namespace grpc_core

grpc_slice grpc_slice_new_with_user_data(void* p, size_t len,
                                         void (*destroy)(void*),
                                         void* user_data) {
  grpc_slice slice;
  slice.refcount =
      grpc_core::New<grpc_core::NewSliceRefcount>(destroy, user_data)
          ->base_refcount();
  slice.data.refcounted.bytes = static_cast<uint8_t*>(p);
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_new(void* p, size_t len, void (*destroy)(void*)) {
  /* Pass "p" to *destroy when the slice is no longer needed. */
  return grpc_slice_new_with_user_data(p, len, destroy, p);
}

namespace grpc_core {
/* grpc_slice_new_with_len support structures - we create a refcount object
   extended with the user provided data pointer & destroy function */
class NewWithLenSliceRefcount {
 public:
  static void Destroy(void* arg) {
    Delete(static_cast<NewWithLenSliceRefcount*>(arg));
  }

  NewWithLenSliceRefcount(void (*destroy)(void*, size_t), void* user_data,
                          size_t user_length)
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_),
        user_data_(user_data),
        user_length_(user_length),
        user_destroy_(destroy) {}

  GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  ~NewWithLenSliceRefcount() { user_destroy_(user_data_, user_length_); }

  grpc_slice_refcount base_;
  RefCount refs_;
  void* user_data_;
  size_t user_length_;
  void (*user_destroy_)(void*, size_t);
};

/** grpc_slice_from_moved_(string|buffer) ref count .*/
class MovedStringSliceRefCount {
 public:
  MovedStringSliceRefCount(grpc_core::UniquePtr<char>&& str)
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_),
        str_(std::move(str)) {}

  GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  static void Destroy(void* arg) {
    Delete(static_cast<MovedStringSliceRefCount*>(arg));
  }

  grpc_slice_refcount base_;
  grpc_core::RefCount refs_;
  grpc_core::UniquePtr<char> str_;
};

}  // namespace grpc_core

grpc_slice grpc_slice_new_with_len(void* p, size_t len,
                                   void (*destroy)(void*, size_t)) {
  grpc_slice slice;
  slice.refcount =
      grpc_core::New<grpc_core::NewWithLenSliceRefcount>(destroy, p, len)
          ->base_refcount();
  slice.data.refcounted.bytes = static_cast<uint8_t*>(p);
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_from_copied_buffer(const char* source, size_t length) {
  if (length == 0) return grpc_empty_slice();
  grpc_slice slice = GRPC_SLICE_MALLOC(length);
  memcpy(GRPC_SLICE_START_PTR(slice), source, length);
  return slice;
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
    slice.refcount =
        grpc_core::New<grpc_core::MovedStringSliceRefCount>(std::move(p))
            ->base_refcount();
    slice.data.refcounted.bytes = ptr;
    slice.data.refcounted.length = len;
  }
  return slice;
}

grpc_slice grpc_slice_from_moved_string(grpc_core::UniquePtr<char> p) {
  const size_t len = strlen(p.get());
  return grpc_slice_from_moved_buffer(std::move(p), len);
}

namespace {

class MallocRefCount {
 public:
  static void Destroy(void* arg) {
    MallocRefCount* r = static_cast<MallocRefCount*>(arg);
    r->~MallocRefCount();
    gpr_free(r);
  }

  MallocRefCount()
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_) {}
  ~MallocRefCount() = default;

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  grpc_slice_refcount base_;
  grpc_core::RefCount refs_;
};

}  // namespace

grpc_slice grpc_slice_malloc_large(size_t length) {
  grpc_slice slice;

  /* Memory layout used by the slice created here:

     +-----------+----------------------------------------------------------+
     | refcount  | bytes                                                    |
     +-----------+----------------------------------------------------------+

     refcount is a malloc_refcount
     bytes is an array of bytes of the requested length
     Both parts are placed in the same allocation returned from gpr_malloc */
  auto* rc =
      static_cast<MallocRefCount*>(gpr_malloc(sizeof(MallocRefCount) + length));

  /* Initial refcount on rc is 1 - and it's up to the caller to release
     this reference. */
  new (rc) MallocRefCount();

  /* Build up the slice to be returned. */
  /* The slices refcount points back to the allocated block. */
  slice.refcount = rc->base_refcount();
  /* The data bytes are placed immediately after the refcount struct */
  slice.data.refcounted.bytes = reinterpret_cast<uint8_t*>(rc + 1);
  /* And the length of the block is set to the requested length */
  slice.data.refcounted.length = length;
  return slice;
}

grpc_slice grpc_slice_malloc(size_t length) {
  grpc_slice slice;

  if (length > sizeof(slice.data.inlined.bytes)) {
    return grpc_slice_malloc_large(length);
  } else {
    /* small slice: just inline the data */
    slice.refcount = nullptr;
    slice.data.inlined.length = static_cast<uint8_t>(length);
  }
  return slice;
}

grpc_slice grpc_slice_sub_no_ref(grpc_slice source, size_t begin, size_t end) {
  grpc_slice subset;

  GPR_ASSERT(end >= begin);

  if (source.refcount) {
    /* Enforce preconditions */
    GPR_ASSERT(source.data.refcounted.length >= end);

    /* Build the result */
    subset.refcount = source.refcount->sub_refcount();
    /* Point into the source array */
    subset.data.refcounted.bytes = source.data.refcounted.bytes + begin;
    subset.data.refcounted.length = end - begin;
  } else {
    /* Enforce preconditions */
    GPR_ASSERT(source.data.inlined.length >= end);
    subset.refcount = nullptr;
    subset.data.inlined.length = static_cast<uint8_t>(end - begin);
    memcpy(subset.data.inlined.bytes, source.data.inlined.bytes + begin,
           end - begin);
  }
  return subset;
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
    /* Bump the refcount */
    subset.refcount->Ref();
  }
  return subset;
}

grpc_slice grpc_slice_split_tail_maybe_ref(grpc_slice* source, size_t split,
                                           grpc_slice_ref_whom ref_whom) {
  grpc_slice tail;

  if (source->refcount == nullptr) {
    /* inlined data, copy it out */
    GPR_ASSERT(source->data.inlined.length >= split);
    tail.refcount = nullptr;
    tail.data.inlined.length =
        static_cast<uint8_t>(source->data.inlined.length - split);
    memcpy(tail.data.inlined.bytes, source->data.inlined.bytes + split,
           tail.data.inlined.length);
    source->data.inlined.length = static_cast<uint8_t>(split);
  } else {
    size_t tail_length = source->data.refcounted.length - split;
    GPR_ASSERT(source->data.refcounted.length >= split);
    if (tail_length < sizeof(tail.data.inlined.bytes) &&
        ref_whom != GRPC_SLICE_REF_TAIL) {
      /* Copy out the bytes - it'll be cheaper than refcounting */
      tail.refcount = nullptr;
      tail.data.inlined.length = static_cast<uint8_t>(tail_length);
      memcpy(tail.data.inlined.bytes, source->data.refcounted.bytes + split,
             tail_length);
      source->refcount = source->refcount->sub_refcount();
    } else {
      /* Build the result */
      switch (ref_whom) {
        case GRPC_SLICE_REF_TAIL:
          tail.refcount = source->refcount->sub_refcount();
          source->refcount = &NoopRefcount;
          break;
        case GRPC_SLICE_REF_HEAD:
          tail.refcount = &NoopRefcount;
          source->refcount = source->refcount->sub_refcount();
          break;
        case GRPC_SLICE_REF_BOTH:
          tail.refcount = source->refcount->sub_refcount();
          source->refcount = source->refcount->sub_refcount();
          /* Bump the refcount */
          tail.refcount->Ref();
          break;
      }
      /* Point into the source array */
      tail.data.refcounted.bytes = source->data.refcounted.bytes + split;
      tail.data.refcounted.length = tail_length;
    }
    source->data.refcounted.length = split;
  }

  return tail;
}

grpc_slice grpc_slice_split_tail(grpc_slice* source, size_t split) {
  return grpc_slice_split_tail_maybe_ref(source, split, GRPC_SLICE_REF_BOTH);
}

grpc_slice grpc_slice_split_head(grpc_slice* source, size_t split) {
  grpc_slice head;

  if (source->refcount == nullptr) {
    GPR_ASSERT(source->data.inlined.length >= split);

    head.refcount = nullptr;
    head.data.inlined.length = static_cast<uint8_t>(split);
    memcpy(head.data.inlined.bytes, source->data.inlined.bytes, split);
    source->data.inlined.length =
        static_cast<uint8_t>(source->data.inlined.length - split);
    memmove(source->data.inlined.bytes, source->data.inlined.bytes + split,
            source->data.inlined.length);
  } else if (split < sizeof(head.data.inlined.bytes)) {
    GPR_ASSERT(source->data.refcounted.length >= split);

    head.refcount = nullptr;
    head.data.inlined.length = static_cast<uint8_t>(split);
    memcpy(head.data.inlined.bytes, source->data.refcounted.bytes, split);
    source->refcount = source->refcount->sub_refcount();
    source->data.refcounted.bytes += split;
    source->data.refcounted.length -= split;
  } else {
    GPR_ASSERT(source->data.refcounted.length >= split);

    /* Build the result */
    head.refcount = source->refcount->sub_refcount();
    /* Bump the refcount */
    head.refcount->Ref();
    /* Point into the source array */
    head.data.refcounted.bytes = source->data.refcounted.bytes;
    head.data.refcounted.length = split;
    source->refcount = source->refcount->sub_refcount();
    source->data.refcounted.bytes += split;
    source->data.refcounted.length -= split;
  }

  return head;
}

int grpc_slice_default_eq_impl(grpc_slice a, grpc_slice b) {
  if (GRPC_SLICE_LENGTH(a) != GRPC_SLICE_LENGTH(b)) return false;
  if (GRPC_SLICE_LENGTH(a) == 0) return true;
  return 0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                     GRPC_SLICE_LENGTH(a));
}

int grpc_slice_eq(grpc_slice a, grpc_slice b) {
  if (a.refcount && b.refcount &&
      a.refcount->GetType() == b.refcount->GetType()) {
    return a.refcount->Eq(a, b);
  }
  return grpc_slice_default_eq_impl(a, b);
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
  for (i = static_cast<int> GRPC_SLICE_LENGTH(s) - 1; i != -1 && b[i] != c; i--)
    ;
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
  if (haystack_len == needle_len)
    return grpc_slice_eq(haystack, needle) ? 0 : -1;
  if (needle_len == 1)
    return grpc_slice_chr(haystack, static_cast<char>(*needle_bytes));

  const uint8_t* last = haystack_bytes + haystack_len - needle_len;
  for (const uint8_t* cur = haystack_bytes; cur != last; ++cur) {
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
