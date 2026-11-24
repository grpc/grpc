//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/grpc/xds_cel_request_resolver.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "cel-c/arena.h"
#include "cel-c/error.h"
#include "cel-c/error_code.h"
#include "cel-c/status.h"
#include "cel-c/string_view_absl.h"
#include "cel-c/value.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

// Sets *value to a copy of s allocated on arena.  Values handed to CEL
// must outlive the resolver call, and RpcMatchContext::GetHeaderValue()
// may return a view into a buffer that is overwritten by the next call.
// Returns false and sets status on allocation failure.
bool SetStringCopy(absl::string_view s, cel_Arena* arena, cel_Value* value,
                   cel_Status* status) {
  if (s.empty()) {
    cel_Value_SetString(value, cel_StringView_FromAbsl(""));
    return true;
  }
  char* data = static_cast<char*>(cel_Arena_Malloc(arena, s.size(), nullptr));
  if (data == nullptr) {
    cel_OutOfMemoryStatus(status);
    return false;
  }
  memcpy(data, s.data(), s.size());
  cel_Value_SetString(value, cel_StringView_FromAbsl({data, s.size()}));
  return true;
}

// Sets *value to the value of the header, if present.  Returns false if
// the header is absent or on allocation failure (in which case status is
// set).
bool SetHeaderValue(const RpcMatchContext& context, absl::string_view header,
                    cel_Arena* arena, cel_Value* value, cel_Status* status) {
  std::optional<absl::string_view> header_value =
      context.GetHeaderValue(header);
  if (!header_value.has_value()) return false;
  return SetStringCopy(*header_value, arena, value, status);
}

// Sets *value to a CEL "no such key" error.  Returns false and sets status
// on allocation failure.
bool SetNoSuchKeyError(cel_Arena* arena, cel_Value* value, cel_Status* status) {
  cel_Error* error = cel_Error_New(arena);
  if (error == nullptr) {
    cel_OutOfMemoryStatus(status);
    return false;
  }
  cel_Error_SetCanonicalCode(error, cel_ErrorCode_kNotFound);
  cel_Error_SetMessage(error, cel_StringView_FromAbsl("no such key"));
  cel_Value_SetError(value, error);
  return true;
}

// A CEL map<string, ...> whose entries are computed on demand from an
// RpcMatchContext.  Attributes must provide:
//
//   // Sets *value to the entry for key.  Returns false if key is absent
//   // or on allocation failure (in which case status is set).
//   static bool Lookup(const RpcMatchContext& context, absl::string_view key,
//                      cel_Arena* arena, cel_Value* value, cel_Status* status);
//
//   // Calls fn once for each key for which Lookup() succeeds.
//   static void ForEachKey(const RpcMatchContext& context,
//                          absl::FunctionRef<void(absl::string_view)> fn);
template <typename Attributes>
class LazyMap {
 public:
  static void Set(const RpcMatchContext& context, cel_Value* value) {
    cel_MapValue* map = cel_Value_SetMap(value);
    map->vtable = &kVTable;
    map->content = {};
    map->content.ptr[0] = &context;
  }

 private:
  struct Iterator {
    cel_MapValueIterator super;
    const RpcMatchContext* context;
    const cel_StringView* keys;
    size_t size;
    size_t next;
  };

  static const RpcMatchContext& GetContext(cel_ValueContent content) {
    return *static_cast<const RpcMatchContext*>(content.ptr[0]);
  }

  static bool Lookup(cel_ValueContent content, const cel_ValueContext* context,
                     const cel_MapValueKey* key, cel_Value* value,
                     cel_Status* status) {
    if (!cel_MapValueKey_IsString(key)) return false;
    return Attributes::Lookup(
        GetContext(content),
        cel_StringView_ToAbsl(cel_MapValueKey_GetString(key)), context->arena,
        value, status);
  }

  static bool FastSize(const cel_MapValueVTable* /*vtable*/,
                       cel_ValueContent /*content*/, size_t* /*size*/) {
    return false;
  }

  static bool SlowSize(const cel_MapValueVTable* /*vtable*/,
                       cel_ValueContent content,
                       const cel_ValueContext* /*context*/, cel_Value* size,
                       cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    int64_t count = 0;
    Attributes::ForEachKey(GetContext(content),
                           [&](absl::string_view) { ++count; });
    cel_Value_SetInt(size, count);
    return true;
  }

  static bool Get(const cel_MapValueVTable* /*vtable*/,
                  cel_ValueContent content, const cel_ValueContext* context,
                  const cel_MapValueKey* key, cel_Value* value,
                  cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    if (Lookup(content, context, key, value, status)) return true;
    if (!cel_Status_Ok(status)) return false;
    return SetNoSuchKeyError(context->arena, value, status);
  }

  static bool Find(const cel_MapValueVTable* /*vtable*/,
                   cel_ValueContent content, const cel_ValueContext* context,
                   const cel_MapValueKey* key, cel_Value* value,
                   cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    return Lookup(content, context, key, value, status);
  }

  static bool Has(const cel_MapValueVTable* /*vtable*/,
                  cel_ValueContent content, const cel_ValueContext* context,
                  const cel_MapValueKey* key, cel_Value* result,
                  cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    cel_Value value;
    bool found = Lookup(content, context, key, &value, status);
    if (!cel_Status_Ok(status)) return false;
    cel_Value_SetBool(result, found);
    return true;
  }

  static cel_MapValueIterator* NewIterator(const cel_MapValueVTable* /*vtable*/,
                                           cel_ValueContent content,
                                           const cel_ValueContext* context,
                                           cel_Status* status) {
    if (!cel_Status_Ok(status)) return nullptr;
    const RpcMatchContext& match_context = GetContext(content);
    std::vector<std::string> keys;
    Attributes::ForEachKey(
        match_context, [&](absl::string_view key) { keys.emplace_back(key); });
    // Everything is allocated on the arena, so no Delete() is needed.
    void* iter_mem =
        cel_Arena_Malloc(context->arena, sizeof(Iterator), nullptr);
    cel_StringView* key_views = static_cast<cel_StringView*>(cel_Arena_Malloc(
        context->arena, sizeof(cel_StringView) * keys.size(), nullptr));
    if (iter_mem == nullptr || (key_views == nullptr && !keys.empty())) {
      cel_OutOfMemoryStatus(status);
      return nullptr;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
      cel_Value key_value;
      if (!SetStringCopy(keys[i], context->arena, &key_value, status)) {
        return nullptr;
      }
      key_views[i] = cel_Value_GetString(&key_value);
    }
    Iterator* iter = new (iter_mem)
        Iterator{{&kIteratorVTable}, &match_context, key_views, keys.size(), 0};
    return &iter->super;
  }

  static Iterator* GetIterator(cel_ValueIterator* iterator) {
    return reinterpret_cast<Iterator*>(iterator);
  }

  // Returns the next key, or nullopt when the iterator is exhausted.
  static std::optional<cel_StringView> NextKey(Iterator* iter) {
    if (iter->next == iter->size) return std::nullopt;
    return iter->keys[iter->next++];
  }

  // Sets *value to the entry for key.  Returns false on failure.
  static bool LookupForIterator(Iterator* iter, cel_StringView key,
                                const cel_ValueContext* context,
                                cel_Value* value, cel_Status* status) {
    if (Attributes::Lookup(*iter->context, cel_StringView_ToAbsl(key),
                           context->arena, value, status)) {
      return true;
    }
    if (!cel_Status_Ok(status)) return false;
    // Should not happen: the key was present when the iterator was created.
    return SetNoSuchKeyError(context->arena, value, status);
  }

  static bool IteratorNext1(cel_ValueIterator* iterator,
                            const cel_ValueContext* /*context*/,
                            cel_Value* key_or_value, cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    std::optional<cel_StringView> key = NextKey(GetIterator(iterator));
    if (!key.has_value()) return false;
    cel_Value_SetString(key_or_value, *key);
    return true;
  }

  static bool IteratorNext2(cel_ValueIterator* iterator,
                            const cel_ValueContext* context, cel_Value* key,
                            cel_Value* value, cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    Iterator* iter = GetIterator(iterator);
    std::optional<cel_StringView> next_key = NextKey(iter);
    if (!next_key.has_value()) return false;
    cel_Value_SetString(key, *next_key);
    return LookupForIterator(iter, *next_key, context, value, status);
  }

  static bool IteratorRemaining(const cel_ValueIterator* iterator,
                                size_t* remaining) {
    const Iterator* iter = reinterpret_cast<const Iterator*>(iterator);
    *remaining = iter->size - iter->next;
    return true;
  }

  static bool IteratorNext(cel_MapValueIterator* iterator,
                           const cel_ValueContext* context,
                           cel_MapValueKey* key, cel_Value* value,
                           cel_Status* status) {
    if (!cel_Status_Ok(status)) return false;
    Iterator* iter = reinterpret_cast<Iterator*>(iterator);
    std::optional<cel_StringView> next_key = NextKey(iter);
    if (!next_key.has_value()) return false;
    if (key != nullptr) cel_MapValueKey_SetString(key, *next_key);
    if (value == nullptr) return true;
    return LookupForIterator(iter, *next_key, context, value, status);
  }

  static constexpr cel_MapValueVTable kVTable = {
      /*Equals=*/nullptr, &FastSize, &SlowSize, &Get, &Find, &Has,
      &NewIterator};

  static constexpr cel_MapValueIteratorVTable kIteratorVTable = {
      /*super=*/{/*Delete=*/nullptr, &IteratorNext1, &IteratorNext2,
                 &IteratorRemaining},
      &IteratorNext};
};

// The `request.headers` map.  Keys are lower-cased header names, as in
// Envoy.
struct HeaderAttributes {
  static bool Lookup(const RpcMatchContext& context, absl::string_view key,
                     cel_Arena* arena, cel_Value* value, cel_Status* status) {
    return SetHeaderValue(context, absl::AsciiStrToLower(key), arena, value,
                          status);
  }

  static void ForEachKey(const RpcMatchContext& context,
                         absl::FunctionRef<void(absl::string_view)> fn) {
    context.ForEachHeaderName(fn);
  }
};

// The `request` map, as defined in gRFC A106.  Attributes that gRPC does
// not support (scheme, time, protocol) are absent.
struct RequestAttributes {
  // Attributes backed by a single header.  These are absent if the header
  // is not present.
  struct HeaderAttribute {
    absl::string_view attribute;
    absl::string_view header;
  };
  static constexpr HeaderAttribute kHeaderAttributes[] = {
      {"path", ":path"},           {"url_path", ":path"},
      {"host", ":authority"},      {"referer", "referer"},
      {"useragent", "user-agent"}, {"id", "x-request-id"},
  };

  static bool Lookup(const RpcMatchContext& context, absl::string_view key,
                     cel_Arena* arena, cel_Value* value, cel_Status* status) {
    for (const HeaderAttribute& attribute : kHeaderAttributes) {
      if (key == attribute.attribute) {
        return SetHeaderValue(context, attribute.header, arena, value, status);
      }
    }
    if (key == "method") {
      // gRPC requests are always POST.
      cel_Value_SetString(value, cel_StringView_FromAbsl("POST"));
      return true;
    }
    if (key == "query") {
      // gRPC requests never have a query string.
      cel_Value_SetString(value, cel_StringView_FromAbsl(""));
      return true;
    }
    if (key == "headers") {
      LazyMap<HeaderAttributes>::Set(context, value);
      return true;
    }
    return false;
  }

  static void ForEachKey(const RpcMatchContext& context,
                         absl::FunctionRef<void(absl::string_view)> fn) {
    for (const HeaderAttribute& attribute : kHeaderAttributes) {
      if (context.GetHeaderValue(attribute.header).has_value()) {
        fn(attribute.attribute);
      }
    }
    fn("method");
    fn("query");
    fn("headers");
  }
};

}  // namespace

CelRequestResolver::CelRequestResolver(const RpcMatchContext& context)
    : context_(context) {
  static constexpr cel_VariableResolverVTable kVTable = {&Find};
  vtable = &kVTable;
}

cel_Trilean CelRequestResolver::Find(const cel_VariableResolver* resolver,
                                     cel_StringView name, cel_Value* value,
                                     cel_Arena* /*arena*/,
                                     cel_Status* /*status*/) {
  if (cel_StringView_ToAbsl(name) != "request") return cel_Trilean_kFalse;
  const auto* self = static_cast<const CelRequestResolver*>(resolver);
  LazyMap<RequestAttributes>::Set(self->context_, value);
  return cel_Trilean_kTrue;
}

}  // namespace grpc_core
