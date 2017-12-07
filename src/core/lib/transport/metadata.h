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

#ifndef GRPC_CORE_LIB_TRANSPORT_METADATA_H
#define GRPC_CORE_LIB_TRANSPORT_METADATA_H

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/any.h"
#include "src/core/lib/support/arena.h"
#include "src/core/lib/support/function.h"
#include "src/core/lib/support/memory.h"

/* This file provides a mechanism for tracking metadata through the grpc stack.
   It's not intended for consumption outside of the library.

   Metadata is tracked in the context of a grpc_mdctx. For the time being there
   is one of these per-channel, avoiding cross channel interference with memory
   use and lock contention.

   The context tracks unique strings (grpc_mdstr) and pairs of strings
   (grpc_mdelem). Any of these objects can be checked for equality by comparing
   their pointers. These objects are reference counted.

   grpc_mdelem can additionally store a (non-NULL) user data pointer. This
   pointer is intended to be used to cache semantic meaning of a metadata
   element. For example, an OAuth token may cache the credentials it represents
   and the time at which it expires in the mdelem user data.

   Combining this metadata cache and the hpack compression table allows us to
   simply lookup complete preparsed objects quickly, incurring a few atomic
   ops per metadata element on the fast path.

   grpc_mdelem instances MAY live longer than their refcount implies, and are
   garbage collected periodically, meaning cached data can easily outlive a
   single request.

   STATIC METADATA: in static_metadata.h we declare a set of static metadata.
   These mdelems and mdstrs are available via pre-declared code generated macros
   and are available to code anywhere between grpc_init() and grpc_shutdown().
   They are not refcounted, but can be passed to _ref and _unref functions
   declared here - in which case those functions are effectively no-ops. */

namespace grpc_core {

extern DebugOnlyTraceFlag grpc_trace_metadata;

namespace metadata {

class Collection;
typedef Any<> AnyValue;

class Key {
 public:
  virtual AnyValue Parse(grpc_slice slice) const = 0;
  virtual bool ParseIntoCollection(grpc_slice slice,
                                   Collection* collection) const = 0;
  virtual bool SetInCollection(const AnyValue& value,
                               Collection* collection) const = 0;
  virtual AnyValue GetFromCollection(const Collection* collection) const = 0;
  virtual bool SerializeValue(const AnyValue& value,
                              grpc_slice_buffer* buffer) const = 0;
  virtual bool SerializeFromCollection(const Collection* collection,
                                       grpc_slice_buffer* buffer) const = 0;
};

template <typename T>
class TypedKey : public Key {
 public:
  virtual void TypedSetInCollection(const T& value,
                                    Collection* collection) const = 0;
  virtual const T* TypedGetFromCollection(
      const Collection* collection) const = 0;
  virtual bool TypedSerializeValue(const T& value,
                                   grpc_slice_buffer* buffer) const = 0;
};

enum class HttpMethod : uint8_t {
  UNSET,
  UNKNOWN,
  GET,
  PUT,
  POST,
};

enum class HttpScheme : uint8_t {
  UNSET,
  UNKNOWN,
  HTTP,
  HTTPS,
  GRPC,
};

enum class HttpTe : uint8_t {
  UNSET,
  UNKNOWN,
  TRAILERS,
};

enum class NamedKeys : int {
  AUTHORITY,
  GRPC_MESSAGE,
  GRPC_PAYLOAD_BIN,
  GRPC_SERVER_STATS_BIN,
  GRPC_TAGS_BIN,
  USER_AGENT,
  HOST,
  COUNT  // must be last
};

enum class ContentType : uint8_t {
  UNSET,
  UNKNOWN,
  APPLICATION_SLASH_GRPC,
};

class Collection {
 public:
  explicit Collection(gpr_arena* arena) : arena_(arena) {}

  bool SetNamedKey(NamedKeys key, grpc_slice slice, bool reset = true) {
    int idx = static_cast<int>(key);
    if (named_keys_[idx] != nullptr) {
      if (!reset) return false;
      grpc_slice_unref_internal(*named_keys_[idx]);
    } else {
      named_keys_[idx] =
          static_cast<grpc_slice*>(gpr_arena_alloc(arena_, sizeof(slice)));
    }
    *named_keys_[idx] = grpc_slice_ref(slice);
    return true;
  }

  bool SetPath(int path, bool reset = true) {
    return SetFn(&path_, path, reset, kPathUnset);
  }

  bool SetStatus(uint16_t status, bool reset = true) {
    return SetFn(&status_, status, reset, kStatusUnset);
  }

  bool SetGrpcStatus(grpc_status_code grpc_status, bool reset = true) {
    return SetFn(&grpc_status_, static_cast<int16_t>(grpc_status), reset,
                 kGrpcStatusUnset);
  }

  bool SetMethod(HttpMethod method, bool reset = true) {
    return SetWithPublicUnset(&method_, method, reset);
  }

  bool SetScheme(HttpScheme scheme, bool reset = true) {
    return SetWithPublicUnset(&scheme_, scheme, reset);
  }

  bool SetTe(HttpTe te, bool reset = true) {
    return SetWithPublicUnset(&te_, te, reset);
  }

  bool SetContentType(ContentType content_type, bool reset = true) {
    return SetWithPublicUnset(&content_type_, content_type, reset);
  }

 private:
  template <class T>
  bool SetFn(T* store, T newval, bool reset, T unset) {
    if (!reset && *store != unset) return false;
    *store = newval;
    return true;
  }

  template <class T>
  bool SetWithPublicUnset(T* store, T newval, bool reset) {
    GPR_ASSERT(newval != T::UNSET);
    if (!reset && *store != T::UNSET) return false;
    *store = newval;
    return true;
  }

  gpr_arena* const arena_;
  grpc_slice* named_keys_[static_cast<int>(NamedKeys::COUNT)] = {nullptr};
  static constexpr int kPathUnset = -1;
  int path_ = kPathUnset;
  static constexpr uint16_t kStatusUnset = 0;
  uint16_t status_ = kStatusUnset;
  static constexpr int16_t kGrpcStatusUnset = -1;
  int16_t grpc_status_ = kGrpcStatusUnset;
  HttpMethod method_ = HttpMethod::UNSET;
  HttpScheme scheme_ = HttpScheme::UNSET;
  HttpTe te_ = HttpTe::UNSET;
  ContentType content_type_ = ContentType::UNSET;
};

namespace impl {

void RegisterKeyType(const char* key_name, Key* key);

}  // namespace impl

template <class KeyType>
const KeyType* RegisterKeyType(const char* key_name) {
  KeyType* k = New<KeyType>();
  impl::RegisterKeyType(key_name, k);
  return k;
}

// given a key name, return a Key implementation
const Key* LookupKey(grpc_slice key_name);

namespace impl {

template <NamedKeys key>
class NamedKey final : public Key {
 public:
  bool ParseIntoCollection(grpc_slice slice, Collection* collection) {
    return collection->SetNamedKey(key, slice, false);
  }
};

template <typename T, bool (Collection::*SetFn)(T, bool),
          bool (Collection::*HasFn)() const,
          const T& (Collection::*GetFn)() const,
          bool (*ParseFn)(grpc_slice slice, T* value),
          bool (*SerializeFn)(const T& value, grpc_slice_buffer* buffer)>
class SpecialKey final : public TypedKey<T> {
 public:
  AnyValue Parse(grpc_slice slice, AnyValue* value) const override {
    T x;
    if (ParseFn(slice, &x)) {
      return AnyValue(std::move(x));
    } else {
      return AnyValue();
    }
  }
  bool ParseIntoCollection(grpc_slice slice,
                           Collection* collection) const override {
    T val;
    if (!ParseFn(slice, &val)) return false;
    return (collection->*SetFn)(val, false);
  }
  bool SetInCollection(const AnyValue& value,
                       Collection* collection) const override {
    const T* p = value.as<T>();
    if (p == nullptr) return false;
    return (collection->*SetFn)(*p, false);
  }
  AnyValue GetFromCollection(const Collection* collection) const override {
    if (!(collection->*HasFn)()) return AnyValue();
    return AnyValue((collection->*GetFn)());
  }
  bool SerializeValue(const AnyValue& value,
                      grpc_slice_buffer* buffer) const override {
    const T* val = value.as<T>();
    if (!val) return false;
    return SerializeFn(*val, buffer);
  }
};

}  // namespace impl

extern const Key* const authority;
extern const Key* const grpc_message;
extern const Key* const grpc_payload_bin;

}  // namespace metadata

}  // namespace grpc_core

/* Forward declarations */
typedef struct grpc_mdelem grpc_mdelem;

/* if changing this, make identical changes in:
   - interned_metadata, allocated_metadata in metadata.c
   - grpc_metadata in grpc_types.h */
typedef struct grpc_mdelem_data {
  const grpc_slice key;
  const grpc_slice value;
  /* there is a private part to this in metadata.c */
} grpc_mdelem_data;

/* GRPC_MDELEM_STORAGE_* enum values that can be treated as interned always have
   this bit set in their integer value */
#define GRPC_MDELEM_STORAGE_INTERNED_BIT 1

typedef enum {
  /* memory pointed to by grpc_mdelem::payload is owned by an external system */
  GRPC_MDELEM_STORAGE_EXTERNAL = 0,
  /* memory pointed to by grpc_mdelem::payload is interned by the metadata
     system */
  GRPC_MDELEM_STORAGE_INTERNED = GRPC_MDELEM_STORAGE_INTERNED_BIT,
  /* memory pointed to by grpc_mdelem::payload is allocated by the metadata
     system */
  GRPC_MDELEM_STORAGE_ALLOCATED = 2,
  /* memory is in the static metadata table */
  GRPC_MDELEM_STORAGE_STATIC = 2 | GRPC_MDELEM_STORAGE_INTERNED_BIT,
} grpc_mdelem_data_storage;

struct grpc_mdelem {
  /* a grpc_mdelem_data* generally, with the two lower bits signalling memory
     ownership as per grpc_mdelem_data_storage */
  uintptr_t payload;
};

#define GRPC_MDELEM_DATA(md) ((grpc_mdelem_data*)((md).payload & ~(uintptr_t)3))
#define GRPC_MDELEM_STORAGE(md) \
  ((grpc_mdelem_data_storage)((md).payload & (uintptr_t)3))
#ifdef __cplusplus
#define GRPC_MAKE_MDELEM(data, storage) \
  (grpc_mdelem{((uintptr_t)(data)) | ((uintptr_t)storage)})
#else
#define GRPC_MAKE_MDELEM(data, storage) \
  ((grpc_mdelem){((uintptr_t)(data)) | ((uintptr_t)storage)})
#endif
#define GRPC_MDELEM_IS_INTERNED(md)          \
  ((grpc_mdelem_data_storage)((md).payload & \
                              (uintptr_t)GRPC_MDELEM_STORAGE_INTERNED_BIT))

/* Unrefs the slices. */
grpc_mdelem grpc_mdelem_from_slices(grpc_slice key, grpc_slice value);

/* Cheaply convert a grpc_metadata to a grpc_mdelem; may use the grpc_metadata
   object as backing storage (so lifetimes should align) */
grpc_mdelem grpc_mdelem_from_grpc_metadata(grpc_metadata* metadata);

/* Does not unref the slices; if a new non-interned mdelem is needed, allocates
   one if compatible_external_backing_store is NULL, or uses
   compatible_external_backing_store if it is non-NULL (in which case it's the
   users responsibility to ensure that it outlives usage) */
grpc_mdelem grpc_mdelem_create(
    grpc_slice key, grpc_slice value,
    grpc_mdelem_data* compatible_external_backing_store);

bool grpc_mdelem_eq(grpc_mdelem a, grpc_mdelem b);

size_t grpc_mdelem_get_size_in_hpack_table(grpc_mdelem elem,
                                           bool use_true_binary_metadata);

/* Mutator and accessor for grpc_mdelem user data. The destructor function
   is used as a type tag and is checked during user_data fetch. */
void* grpc_mdelem_get_user_data(grpc_mdelem md, void (*if_destroy_func)(void*));
void* grpc_mdelem_set_user_data(grpc_mdelem md, void (*destroy_func)(void*),
                                void* user_data);

#ifndef NDEBUG
#define GRPC_MDELEM_REF(s) grpc_mdelem_ref((s), __FILE__, __LINE__)
#define GRPC_MDELEM_UNREF(s) grpc_mdelem_unref((s), __FILE__, __LINE__)
grpc_mdelem grpc_mdelem_ref(grpc_mdelem md, const char* file, int line);
void grpc_mdelem_unref(grpc_mdelem md, const char* file, int line);
#else
#define GRPC_MDELEM_REF(s) grpc_mdelem_ref((s))
#define GRPC_MDELEM_UNREF(s) grpc_mdelem_unref((s))
grpc_mdelem grpc_mdelem_ref(grpc_mdelem md);
void grpc_mdelem_unref(grpc_mdelem md);
#endif

#define GRPC_MDKEY(md) (GRPC_MDELEM_DATA(md)->key)
#define GRPC_MDVALUE(md) (GRPC_MDELEM_DATA(md)->value)

#define GRPC_MDNULL GRPC_MAKE_MDELEM(NULL, GRPC_MDELEM_STORAGE_EXTERNAL)
#define GRPC_MDISNULL(md) (GRPC_MDELEM_DATA(md) == NULL)

/* We add 32 bytes of padding as per RFC-7540 section 6.5.2. */
#define GRPC_MDELEM_LENGTH(e)                                                  \
  (GRPC_SLICE_LENGTH(GRPC_MDKEY((e))) + GRPC_SLICE_LENGTH(GRPC_MDVALUE((e))) + \
   32)

#define GRPC_MDSTR_KV_HASH(k_hash, v_hash) (GPR_ROTL((k_hash), 2) ^ (v_hash))

void grpc_mdctx_global_init(void);
void grpc_mdctx_global_shutdown();

#define GRPC__CAN_INCLUDE_STATIC_METADATA_H
#include "src/core/lib/transport/static_metadata.h"
#undef GRPC__CAN_INCLUDE_STATIC_METADATA_H

typedef struct grpc_linked_mdelem {
  grpc_mdelem md;
  struct grpc_linked_mdelem* next;
  struct grpc_linked_mdelem* prev;
  void* reserved;
} grpc_linked_mdelem;

typedef struct grpc_mdelem_list {
  size_t count;
  size_t default_count;  // Number of default keys.
  grpc_linked_mdelem* head;
  grpc_linked_mdelem* tail;
} grpc_mdelem_list;

typedef struct grpc_metadata_batch {
  /** Metadata elements in this batch */
  grpc_mdelem_list list;
  grpc_metadata_batch_callouts idx;
  /** Used to calculate grpc-timeout at the point of sending,
      or GRPC_MILLIS_INF_FUTURE if this batch does not need to send a
      grpc-timeout */
  grpc_millis deadline;
} grpc_metadata_batch;

void grpc_metadata_batch_init(grpc_metadata_batch* batch);
void grpc_metadata_batch_destroy(grpc_metadata_batch* batch);
void grpc_metadata_batch_clear(grpc_metadata_batch* batch);
bool grpc_metadata_batch_is_empty(grpc_metadata_batch* batch);

/* Returns the transport size of the batch. */
size_t grpc_metadata_batch_size(grpc_metadata_batch* batch);

/** Remove \a storage from the batch, unreffing the mdelem contained */
void grpc_metadata_batch_remove(grpc_metadata_batch* batch,
                                grpc_linked_mdelem* storage);

/** Substitute a new mdelem for an old value */
grpc_error* grpc_metadata_batch_substitute(grpc_metadata_batch* batch,
                                           grpc_linked_mdelem* storage,
                                           grpc_mdelem new_value);

void grpc_metadata_batch_set_value(grpc_linked_mdelem* storage,
                                   grpc_slice value);

/** Add \a storage to the beginning of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
grpc_error* grpc_metadata_batch_link_head(grpc_metadata_batch* batch,
                                          grpc_linked_mdelem* storage)
    GRPC_MUST_USE_RESULT;
/** Add \a storage to the end of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
grpc_error* grpc_metadata_batch_link_tail(grpc_metadata_batch* batch,
                                          grpc_linked_mdelem* storage)
    GRPC_MUST_USE_RESULT;

/** Add \a elem_to_add as the first element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
grpc_error* grpc_metadata_batch_add_head(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_mdelem elem_to_add) GRPC_MUST_USE_RESULT;
/** Add \a elem_to_add as the last element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
grpc_error* grpc_metadata_batch_add_tail(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_mdelem elem_to_add) GRPC_MUST_USE_RESULT;

grpc_error* grpc_attach_md_to_error(grpc_error* src, grpc_mdelem md);

typedef struct {
  grpc_error* error;
  grpc_mdelem md;
} grpc_filtered_mdelem;

#define GRPC_FILTERED_ERROR(error) \
  { (error), GRPC_MDNULL }
#define GRPC_FILTERED_MDELEM(md) \
  { GRPC_ERROR_NONE, (md) }
#define GRPC_FILTERED_REMOVE() \
  { GRPC_ERROR_NONE, GRPC_MDNULL }

typedef grpc_filtered_mdelem (*grpc_metadata_batch_filter_func)(
    void* user_data, grpc_mdelem elem);
grpc_error* grpc_metadata_batch_filter(
    grpc_metadata_batch* batch, grpc_metadata_batch_filter_func func,
    void* user_data, const char* composite_error_string) GRPC_MUST_USE_RESULT;

#ifndef NDEBUG
void grpc_metadata_batch_assert_ok(grpc_metadata_batch* comd);
#else
#define grpc_metadata_batch_assert_ok(comd) \
  do {                                      \
  } while (0)
#endif

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_H */
