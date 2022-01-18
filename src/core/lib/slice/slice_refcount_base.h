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

#ifndef GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_BASE_H
#define GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_BASE_H

#include <grpc/support/port_platform.h>

#include <atomic>

#include <grpc/slice.h>
#include <grpc/support/log.h>

// grpc_slice_refcount : A reference count for grpc_slice.
//
// Non-inlined grpc_slice objects are refcounted. Historically this was
// implemented via grpc_slice_refcount, a C-style polymorphic class using a
// manually managed vtable of operations. Subclasses would define their own
// vtable; the 'virtual' methods (ref, unref, equals and hash) would simply call
// the function pointers in the vtable as necessary.
//
// Unfortunately, this leads to some inefficiencies in the generated code that
// can be improved upon. For example, equality checking for interned slices is a
// simple equality check on the refcount pointer. With the vtable approach, this
// would translate to roughly the following (high-level) instructions:
//
// grpc_slice_equals(slice1, slice2):
//   load vtable->eq -> eq_func
//   call eq_func(slice1, slice2)
//
// interned_slice_equals(slice1, slice2)
//   load slice1.ref -> r1
//   load slice2.ref -> r2
//   cmp r1, r2 -> retval
//   ret retval
//
// This leads to a function call for a function defined in another translation
// unit, which imposes memory barriers, which reduces the compiler's ability to
// optimize (in addition to the added overhead of call/ret). Additionally, it
// may be harder to reason about branch prediction when we're jumping to
// essentially arbitrarily provided function pointers.
//
// In addition, it is arguable that while virtualization was helpful for
// Equals()/Hash() methods, that it was fundamentally unnecessary for
// Ref()/Unref().
//
// Instead, grpc_slice_refcount provides the same functionality as the C-style
// virtual class, but in a de-virtualized manner - Eq(), Hash(), Ref() and
// Unref() are provided within this header file. Fastpaths for Eq()/Hash()
// (interned and static metadata slices), as well as the Ref() operation, can
// all be inlined without any memory barriers.
//
// It does this by:
// 1. Using grpc_core::RefCount<> (header-only) for Ref/Unref. Two special cases
//    need support: No-op ref/unref (eg. static metadata slices) and stream
//    slice references (where all the slices share the streamref). This is in
//    addition to the normal case of '1 slice, 1 ref'.
//    To support these cases, we explicitly track a nullable pointer to the
//    underlying RefCount<>. No-op ref/unref is used by checking the pointer for
//    null, and doing nothing if it is. Both stream slice refs and 'normal'
//    slices use the same path for Ref/Unref (by targeting the non-null
//    pointer).
//
// 2. introducing the notion of grpc_slice_refcount::Type. This describes if a
//    slice ref is used by a static metadata slice, an interned slice, or other
//    slices. We switch on the slice ref type in order to provide fastpaths for
//    Equals() and Hash().
//
// In total, this saves us roughly 1-2% latency for unary calls, with smaller
// calls benefitting. The effect is present, but not as useful, for larger calls
// where the cost of sending the data dominates.
// TODO(arjunroy): Investigate if this can be removed with strongly typed
// grpc_slices.
struct grpc_slice_refcount {
 public:
  enum class Type {
    INTERNED,  // Refcount for an interned slice.
    NOP,       // No-Op
    REGULAR    // Refcount for non-static-metadata, non-interned slices.
  };
  typedef void (*DestroyerFn)(void*);

  grpc_slice_refcount() = default;

  explicit grpc_slice_refcount(Type t) : ref_type_(t) {}

  explicit grpc_slice_refcount(grpc_slice_refcount* sub) : sub_refcount_(sub) {}
  // Regular constructor for grpc_slice_refcount.
  //
  // Parameters:
  //  1. grpc_slice_refcount::Type type
  //  Whether we are the refcount for a static
  //  metadata slice, an interned slice, or any other kind of slice.
  //
  //  2. std::atomic<size_t>* ref
  //  The pointer to the actual underlying grpc_core::RefCount.
  //  TODO(ctiller): remove the pointer indirection and just put the refcount on
  //  this object once we remove interning.
  //
  //  3. DestroyerFn destroyer_fn
  //  Called when the refcount goes to 0, with destroyer_arg as parameter.
  //
  //  4. void* destroyer_arg
  //  Argument for the virtualized destructor.
  //
  //  5. grpc_slice_refcount* sub
  //  Argument used for interned slices.
  grpc_slice_refcount(grpc_slice_refcount::Type type, std::atomic<size_t>* ref,
                      DestroyerFn destroyer_fn, void* destroyer_arg,
                      grpc_slice_refcount* sub)
      : ref_(ref),
        ref_type_(type),
        sub_refcount_(sub),
        dest_fn_(destroyer_fn),
        destroy_fn_arg_(destroyer_arg) {}
  // Initializer for static refcounts.
  grpc_slice_refcount(grpc_slice_refcount* sub, Type type)
      : ref_type_(type), sub_refcount_(sub) {}

  Type GetType() const { return ref_type_; }

  int Eq(const grpc_slice& a, const grpc_slice& b);

  uint32_t Hash(const grpc_slice& slice);
  void Ref() {
    if (ref_ == nullptr) return;
    ref_->fetch_add(1, std::memory_order_relaxed);
  }
  void Unref() {
    if (ref_ == nullptr) return;
    if (ref_->fetch_sub(1, std::memory_order_acq_rel) == 1) {
      dest_fn_(destroy_fn_arg_);
    }
  }

  // Only for type REGULAR, is this the only instance?
  // For this to be useful the caller needs to ensure that if this is the only
  // instance, no other instance could be created during this call.
  bool IsRegularUnique() {
    GPR_DEBUG_ASSERT(ref_type_ == Type::REGULAR);
    return ref_->load(std::memory_order_relaxed) == 1;
  }

  grpc_slice_refcount* sub_refcount() const { return sub_refcount_; }

 private:
  std::atomic<size_t>* ref_ = nullptr;
  const Type ref_type_ = Type::REGULAR;
  grpc_slice_refcount* sub_refcount_ = this;
  DestroyerFn dest_fn_ = nullptr;
  void* destroy_fn_arg_ = nullptr;
};

#endif  // GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_BASE_H
