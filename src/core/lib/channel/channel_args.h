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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_ARGS_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_ARGS_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <algorithm>  // IWYU pragma: keep
#include <string>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/avl/avl.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/surface/channel_stack_type.h"

// Channel args are intentionally immutable, to avoid the need for locking.

namespace grpc_core {

// Define a traits object for vtable lookup - allows us to integrate with
// existing code easily (just define the trait!) and allows some magic in
// ChannelArgs to automatically derive a vtable from a T*.
// To participate as a pointer, instances should expose the function:
//   // Gets the vtable for this type
//   static const grpc_channel_arg_vtable* VTable();
//   // Performs any mutations required for channel args to own a pointer
//   // Only needed if ChannelArgs::Set is to be called with a raw pointer.
//   static void* TakeUnownedPointer(T* p);
template <typename T, typename Ignored = void /* for SFINAE */>
struct ChannelArgTypeTraits;

// Specialization for ref-counted pointers.
// Types should expose:
// static int ChannelArgsCompare(const T* a, const T* b);
template <typename T>
struct ChannelArgTypeTraits<
    T,
    absl::enable_if_t<
        std::is_base_of<RefCounted<T>, T>::value ||
            std::is_base_of<RefCounted<T, NonPolymorphicRefCount>, T>::value ||
            std::is_base_of<DualRefCounted<T>, T>::value,
        void>> {
  static const grpc_arg_pointer_vtable* VTable() {
    static const grpc_arg_pointer_vtable tbl = {
        // copy
        [](void* p) -> void* { return static_cast<T*>(p)->Ref().release(); },
        // destroy
        [](void* p) { static_cast<T*>(p)->Unref(); },
        // compare
        [](void* p1, void* p2) {
          return T::ChannelArgsCompare(static_cast<const T*>(p1),
                                       static_cast<const T*>(p2));
        },
    };
    return &tbl;
  };
};

// If a type declares some member 'struct RawPointerChannelArgTag {}' then
// we automatically generate a vtable for it that does not do any ownership
// management and compares the type by pointer identity.
// This is intended to be relatively ugly because *most types should worry about
// ownership*.
template <typename T>
struct ChannelArgTypeTraits<T,
                            absl::void_t<typename T::RawPointerChannelArgTag>> {
  static void* TakeUnownedPointer(T* p) { return p; }
  static const grpc_arg_pointer_vtable* VTable() {
    static const grpc_arg_pointer_vtable tbl = {
        // copy
        [](void* p) -> void* { return p; },
        // destroy
        [](void*) {},
        // compare
        [](void* p1, void* p2) { return QsortCompare(p1, p2); },
    };
    return &tbl;
  };
};

class ChannelArgs {
 public:
  class Pointer {
   public:
    Pointer(void* p, const grpc_arg_pointer_vtable* vtable)
        : p_(p), vtable_(vtable == nullptr ? EmptyVTable() : vtable) {}
    ~Pointer() { vtable_->destroy(p_); }

    Pointer(const Pointer& other)
        : p_(other.vtable_->copy(other.p_)), vtable_(other.vtable_) {}
    Pointer& operator=(Pointer other) {
      std::swap(p_, other.p_);
      std::swap(vtable_, other.vtable_);
      return *this;
    }
    Pointer(Pointer&& other) noexcept : p_(other.p_), vtable_(other.vtable_) {
      other.p_ = nullptr;
      other.vtable_ = EmptyVTable();
    }
    Pointer& operator=(Pointer&& other) noexcept {
      std::swap(p_, other.p_);
      std::swap(vtable_, other.vtable_);
      return *this;
    }

    bool operator==(const Pointer& rhs) const;
    bool operator<(const Pointer& rhs) const;
    bool operator!=(const Pointer& rhs) const { return !(*this == rhs); }

    void* c_pointer() const { return p_; }

    const grpc_arg_pointer_vtable* c_vtable() const { return vtable_; }

   private:
    static const grpc_arg_pointer_vtable* EmptyVTable() {
      static const grpc_arg_pointer_vtable vtable = {
          // copy
          [](void* p) { return p; },
          // destroy
          [](void*) {},
          // cmp
          [](void* p1, void* p2) -> int { return QsortCompare(p1, p2); },
      };
      return &vtable;
    }

    void* p_;
    const grpc_arg_pointer_vtable* vtable_;
  };
  using Value = absl::variant<int, std::string, Pointer>;

  ChannelArgs();

  static ChannelArgs FromC(const grpc_channel_args* args);
  // Construct a new grpc_channel_args struct which the caller will own.
  // It should be destroyed with grpc_channel_args_destroy.
  const grpc_channel_args* ToC() const;

  const Value* Get(absl::string_view name) const { return args_.Lookup(name); }
  GRPC_MUST_USE_RESULT ChannelArgs Set(absl::string_view name,
                                       Value value) const;
  GRPC_MUST_USE_RESULT ChannelArgs Set(absl::string_view name,
                                       absl::string_view value) const;
  GRPC_MUST_USE_RESULT ChannelArgs Set(absl::string_view name,
                                       std::string value) const;
  GRPC_MUST_USE_RESULT ChannelArgs Set(absl::string_view name,
                                       const char* value) const;
  GRPC_MUST_USE_RESULT ChannelArgs Set(grpc_arg arg) const;
  template <typename T>
  GRPC_MUST_USE_RESULT absl::enable_if_t<
      std::is_same<const grpc_arg_pointer_vtable*,
                   decltype(ChannelArgTypeTraits<T>::VTable())>::value,
      ChannelArgs>
  Set(absl::string_view name, T* value) const {
    return Set(name, Pointer(ChannelArgTypeTraits<T>::TakeUnownedPointer(value),
                             ChannelArgTypeTraits<T>::VTable()));
  }
  template <typename T>
  GRPC_MUST_USE_RESULT auto Set(absl::string_view name,
                                const RefCountedPtr<T>& value) const
      -> absl::enable_if_t<
          std::is_same<
              const grpc_arg_pointer_vtable*,
              decltype(ChannelArgTypeTraits<absl::remove_cvref_t<
                           decltype(*value->Ref())>>::VTable())>::value,
          ChannelArgs> {
    auto store_value = value->Ref();
    return Set(
        name,
        Pointer(store_value.release(),
                ChannelArgTypeTraits<
                    absl::remove_cvref_t<decltype(*store_value)>>::VTable()));
  }
  template <typename T>
  GRPC_MUST_USE_RESULT ChannelArgs SetIfUnset(absl::string_view name, T value) {
    if (Contains(name)) return *this;
    return Set(name, std::move(value));
  }
  GRPC_MUST_USE_RESULT ChannelArgs Remove(absl::string_view name) const;
  bool Contains(absl::string_view name) const { return Get(name) != nullptr; }

  absl::optional<int> GetInt(absl::string_view name) const;
  absl::optional<absl::string_view> GetString(absl::string_view name) const;
  void* GetVoidPointer(absl::string_view name) const;
  template <typename T>
  T* GetPointer(absl::string_view name) const {
    return static_cast<T*>(GetVoidPointer(name));
  }
  absl::optional<Duration> GetDurationFromIntMillis(
      absl::string_view name) const;
  absl::optional<bool> GetBool(absl::string_view name) const;

  // Object based get/set.
  // Deal with the common case that we set a pointer to an object under
  // the same name in every usage.
  // Expects ChannelArgTypeTraits to exist for T, and T to expose:
  //   static string_view ChannelArgName();
  template <typename T>
  GRPC_MUST_USE_RESULT ChannelArgs SetObject(T* p) const {
    return Set(T::ChannelArgName(), p);
  }
  template <typename T>
  GRPC_MUST_USE_RESULT ChannelArgs SetObject(RefCountedPtr<T> p) const {
    return Set(T::ChannelArgName(), std::move(p));
  }
  template <typename T>
  T* GetObject() {
    return GetPointer<T>(T::ChannelArgName());
  }
  template <typename T>
  RefCountedPtr<T> GetObjectRef() {
    auto* p = GetObject<T>();
    if (p == nullptr) return nullptr;
    return p->Ref();
  }

  bool operator<(const ChannelArgs& other) const { return args_ < other.args_; }
  bool operator==(const ChannelArgs& other) const {
    return args_ == other.args_;
  }

  // Helpers for commonly accessed things

  bool WantMinimalStack() const {
    return GetBool(GRPC_ARG_MINIMAL_STACK).value_or(false);
  }

  std::string ToString() const;

 private:
  explicit ChannelArgs(AVL<std::string, Value> args) : args_(std::move(args)) {}

  AVL<std::string, Value> args_;
};

}  // namespace grpc_core

/** Copy the arguments in \a src into a new instance */
grpc_channel_args* grpc_channel_args_copy(const grpc_channel_args* src);

/** Copy the arguments in \a src into a new instance, stably sorting keys */
grpc_channel_args* grpc_channel_args_normalize(const grpc_channel_args* src);

/** Copy the arguments in \a src and append \a to_add. If \a to_add is NULL, it
 * is equivalent to calling \a grpc_channel_args_copy. */
grpc_channel_args* grpc_channel_args_copy_and_add(const grpc_channel_args* src,
                                                  const grpc_arg* to_add,
                                                  size_t num_to_add);

/** Copies the arguments in \a src except for those whose keys are in
    \a to_remove. */
grpc_channel_args* grpc_channel_args_copy_and_remove(
    const grpc_channel_args* src, const char** to_remove, size_t num_to_remove);

/** Copies the arguments from \a src except for those whose keys are in
    \a to_remove and appends the arguments in \a to_add. */
grpc_channel_args* grpc_channel_args_copy_and_add_and_remove(
    const grpc_channel_args* src, const char** to_remove, size_t num_to_remove,
    const grpc_arg* to_add, size_t num_to_add);

/** Perform the union of \a a and \a b, prioritizing \a a entries */
grpc_channel_args* grpc_channel_args_union(const grpc_channel_args* a,
                                           const grpc_channel_args* b);

/** Destroy arguments created by \a grpc_channel_args_copy */
void grpc_channel_args_destroy(grpc_channel_args* a);
inline void grpc_channel_args_destroy(const grpc_channel_args* a) {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(a));
}

int grpc_channel_args_compare(const grpc_channel_args* a,
                              const grpc_channel_args* b);

/** Returns the value of argument \a name from \a args, or NULL if not found. */
const grpc_arg* grpc_channel_args_find(const grpc_channel_args* args,
                                       const char* name);

bool grpc_channel_args_want_minimal_stack(const grpc_channel_args* args);

typedef struct grpc_integer_options {
  int default_value;  // Return this if value is outside of expected bounds.
  int min_value;
  int max_value;
} grpc_integer_options;

/** Returns the value of \a arg, subject to the constraints in \a options. */
int grpc_channel_arg_get_integer(const grpc_arg* arg,
                                 const grpc_integer_options options);
/** Similar to the above, but needs to find the arg from \a args by the name
 * first. */
int grpc_channel_args_find_integer(const grpc_channel_args* args,
                                   const char* name,
                                   const grpc_integer_options options);

/** Returns the value of \a arg if \a arg is of type GRPC_ARG_STRING.
    Otherwise, emits a warning log, and returns nullptr.
    If arg is nullptr, returns nullptr, and does not emit a warning. */
char* grpc_channel_arg_get_string(const grpc_arg* arg);
/** Similar to the above, but needs to find the arg from \a args by the name
 * first. */
char* grpc_channel_args_find_string(const grpc_channel_args* args,
                                    const char* name);
/** If \a arg is of type GRPC_ARG_INTEGER, returns true if it's non-zero.
 * Returns \a default_value if \a arg is of other types. */
bool grpc_channel_arg_get_bool(const grpc_arg* arg, bool default_value);
/** Similar to the above, but needs to find the arg from \a args by the name
 * first. */
bool grpc_channel_args_find_bool(const grpc_channel_args* args,
                                 const char* name, bool default_value);

template <typename T>
T* grpc_channel_args_find_pointer(const grpc_channel_args* args,
                                  const char* name) {
  const grpc_arg* arg = grpc_channel_args_find(args, name);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) return nullptr;
  return static_cast<T*>(arg->value.pointer.p);
}

// Helpers for creating channel args.
grpc_arg grpc_channel_arg_string_create(char* name, char* value);
grpc_arg grpc_channel_arg_integer_create(char* name, int value);
grpc_arg grpc_channel_arg_pointer_create(char* name, void* value,
                                         const grpc_arg_pointer_vtable* vtable);

// Returns a string representing channel args in human-readable form.
std::string grpc_channel_args_string(const grpc_channel_args* args);

namespace grpc_core {
// Ensure no duplicate channel args (with some backwards compatibility hacks).
// Eliminate any grpc.internal.* args.
// Return a C++ object.
ChannelArgs ChannelArgsBuiltinPrecondition(const grpc_channel_args* src);
}  // namespace grpc_core

// Takes ownership of the old_args
typedef grpc_core::ChannelArgs (
    *grpc_channel_args_client_channel_creation_mutator)(
    const char* target, grpc_core::ChannelArgs old_args,
    grpc_channel_stack_type type);

// Should be called only once globaly before grpc is init'ed.
void grpc_channel_args_set_client_channel_creation_mutator(
    grpc_channel_args_client_channel_creation_mutator cb);
// This will be called at the creation of each channel.
grpc_channel_args_client_channel_creation_mutator
grpc_channel_args_get_client_channel_creation_mutator();

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_ARGS_H */
