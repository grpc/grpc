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
  virtual grpc_error* Parse(grpc_slice slice, AnyValue* value) const = 0;
  virtual grpc_error* ParseIntoCollection(grpc_slice slice,
                                          Collection* collection) const = 0;
  virtual grpc_error* SetInCollection(const AnyValue& value,
                                      Collection* collection) const = 0;
  virtual AnyValue GetFromCollection(const Collection* collection) const = 0;
  virtual bool SerializeValue(const AnyValue& value,
                              grpc_slice_buffer* buffer) const = 0;
  virtual bool SerializeFromCollection(const Collection* collection,
                                       grpc_slice_buffer* buffer) const = 0;
  virtual uint32_t SizeInHpackTable(const AnyValue& value) const = 0;
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

enum class NamedKeys : uint8_t {
  AUTHORITY,
  // non colon prefixed names start here
  USER_AGENT,
  GRPC_MESSAGE,
  GRPC_PAYLOAD_BIN,
  GRPC_SERVER_STATS_BIN,
  GRPC_TAGS_BIN,
  HOST,
  COUNT  // must be last
};

grpc_slice NamedKeyKey(NamedKeys k);

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

  bool HasPath() const { return path_ != kPathUnset; }

  int Path() const {
    assert(path_ != kPathUnset);
    return path_;
  }

  bool SetStatus(uint16_t status, bool reset = true) {
    return SetFn(&status_, status, reset, kStatusUnset);
  }

  bool HasStatus() const { return status_ != kStatusUnset; }

  uint16_t Status() const {
    assert(status_ != kStatusUnset);
    return status_;
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

  template <class CB>
  void ForEachField(CB* cb) const {
    if (path_ != kPathUnset) cb->OnPath(path_);
    if (status_ != kStatusUnset) cb->OnStatus(status_);
    if (method_ != HttpMethod::UNSET) cb->OnMethod(method_);
    if (scheme_ != HttpScheme::UNSET) cb->OnScheme(scheme_);
    for (int i = 0; i < static_cast<int>(NamedKeys::COUNT); i++) {
      if (named_keys_[i] != nullptr) {
        cb->OnNamedKey(static_cast<NamedKeys>(i), *named_keys_[i]);
      }
    }
    if (te_ != HttpTe::UNSET) cb->OnTe(te_);
    if (content_type_ != ContentType::UNSET) cb->OnContentType(content_type_);
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
  // http status
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

int InternPath(const char* path);
grpc_slice PathSlice(int idx);

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

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_H */
