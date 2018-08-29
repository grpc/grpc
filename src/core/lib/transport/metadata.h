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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_metadata;

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

#define MIN_STATIC_HPACK_TABLE_IDX 1
#define MAX_STATIC_HPACK_TABLE_IDX 61

/* Static hpack table metadata indices */

/* {:authority, ""} */
#define GRPC_MDELEM_AUTHORITY_EMPTY_INDEX 1

/* {":method", "GET"} */
#define GRPC_MDELEM_METHOD_GET_INDEX 2

/* {":method", "POST"} */
#define GRPC_MDELEM_METHOD_POST_INDEX 3

/* {":path", "/"} */
#define GRPC_MDELEM_PATH_SLASH_INDEX 4

/* {":path", "/index.html"} */
#define GRPC_MDELEM_PATH_SLASH_INDEX_DOT_HTML_INDEX 5

/* {":scheme", "http"} */
#define GRPC_MDELEM_SCHEME_HTTP_INDEX 6

/* {":scheme", "https"} */
#define GRPC_MDELEM_SCHEME_HTTPS_INDEX 7

/* {":status", "200"} */
#define GRPC_MDELEM_STATUS_200_INDEX 8

/* {":status", "204"} */
#define GRPC_MDELEM_STATUS_204_INDEX 9

/* {":status", "206"} */
#define GRPC_MDELEM_STATUS_206_INDEX 10

/* {":status", "304"} */
#define GRPC_MDELEM_STATUS_304_INDEX 11

/* {":status", "400"} */
#define GRPC_MDELEM_STATUS_400_INDEX 12

/* {":status", "404"} */
#define GRPC_MDELEM_STATUS_404_INDEX 13

/* {":status", "500"} */
#define GRPC_MDELEM_STATUS_500_INDEX 14

/* {"accept-charset", ""} */
#define GRPC_MDELEM_ACCEPT_CHARSET_EMPTY_INDEX 15

/* {"accept-encoding", "gzip, deflate"} */
#define GRPC_MDELEM_ACCEPT_ENCODING_GZIP_DEFLATE_INDEX 16

/* {"accept-language", ""} */
#define GRPC_MDELEM_MDELEM_ACCEPT_LANGUAGE_EMPTY_INDEX 17

/* {"accept-ranges", ""} */
#define GRPC_MDELEM_MDELEM_ACCEPT_RANGES_EMPTY_INDEX 18

/* {"accept", ""} */
#define GRPC_MDELEM_ACCEPT_EMPTY_INDEX 19

/* {"access-control-allow-origin", ""} */
#define GRPC_MDELEM_ACCESS_CONTROL_ALLOW_ORIGIN_EMPTY_INDEX 20

/* {"age", ""} */
#define GRPC_MDELEM_AGE_EMPTY_INDEX 21

/* {"allow", ""} */
#define GRPC_MDELEM_ALLOW_EMPTY_INDEX 22

/* {"authorization", ""} */
#define GRPC_MDELEM_AUTHORIZATION_EMPTY_INDEX 23

/* {"cache-control", ""} */
#define GRPC_MDELEM_CACHE_CONTROL_EMPTY_INDEX 24

/* {"content-disposition", ""} */
#define GRPC_MDELEM_CONTENT_DISPOSITION_EMPTY_INDEX 25

/* {"content-encoding", ""} */
#define GRPC_MDELEM_CONTENT_ENCODING_EMPTY_INDEX 26

/* {"content-language", ""} */
#define GRPC_MDELEM_CONTENT_LANGUAGE_EMPTY_INDEX 27

/* {"content-length", ""} */
#define GRPC_MDELEM_CONTENT_LENGTH_EMPTY_INDEX 28

/* {"content-location", ""} */
#define GRPC_MDELEM_CONTENT_LOCATION_EMPTY_INDEX 29

/* {"content-range", ""} */
#define GRPC_MDELEM_CONTENT_RANGE_EMPTY_INDEX 30

/* {"content-type", ""} */
#define GRPC_MDELEM_CONTENT_TYPE_EMPTY_INDEX 31

/* {"cookie", ""} */
#define GRPC_MDELEM_COOKIE_EMPTY_INDEX 32

/* {"date", ""} */
#define GRPC_MDELEM_DATE_EMPTY_INDEX 33

/* {"etag", ""} */
#define GRPC_MDELEM_ETAG_EMPTY_INDEX 34

/* {"expect", ""} */
#define GRPC_MDELEM_EXPECT_EMPTY_INDEX 35

/* {"expires", ""} */
#define GRPC_MDELEM_EXPIRES_EMPTY_INDEX 36

/* {"from", ""} */
#define GRPC_MDELEM_FROM_EMPTY_INDEX 37

/* {"host", ""} */
#define GRPC_MDELEM_HOST_EMPTY_INDEX 38

/* {"if-match", ""} */
#define GRPC_MDELEM_IF_MATCH_EMPTY_INDEX 39

/* {"if-modified-since", ""} */
#define GRPC_MDELEM_IF_MODIFIED_SINCE_EMPTY_INDEX 40

/* {"if-none-match", ""} */
#define GRPC_MDELEM_IF_NONE_MATCH_EMPTY_INDEX 41

/* {"if-range", ""} */
#define GRPC_MDELEM_IF_RANGE_EMPTY_INDEX 42

/* {"if-unmodified-since", ""} */
#define GRPC_MDELEM_IF_UNMODIFIED_SINCE_EMPTY_INDEX 43

/* {"last-modified", ""} */
#define GRPC_MDELEM_LAST_MODIFIED_EMPTY_INDEX 44

/* {"link", ""} */
#define GRPC_MDELEM_LINK_EMPTY_INDEX 45

/* {"location", ""} */
#define GRPC_MDELEM_LOCATION_EMPTY_INDEX 46

/* {"max-forwards", ""} */
#define GRPC_MDELEM_MAX_FORWARDS_EMPTY_INDEX 47

/* {"proxy-authenticate", ""} */
#define GRPC_MDELEM_PROXY_AUTHENTICATE_EMPTY_INDEX 48

/* {"proxy-authorization", ""} */
#define GRPC_MDELEM_PROXY_AUTHORIZATION_EMPTY_INDEX 49

/* {"range", ""} */
#define GRPC_MDELEM_RANGE_EMPTY_INDEX 50

/* {"referer", ""} */
#define GRPC_MDELEM_REFERER_EMPTY_INDEX 51

/* {"refresh", ""} */
#define GRPC_MDELEM_REFRESH_EMPTY_INDEX 52

/* {"retry-after", ""} */
#define GRPC_MDELEM_RETRY_AFTER_EMPTY_INDEX 53

/* {"server", ""} */
#define GRPC_MDELEM_SERVER_EMPTY_INDEX 54

/* {"set-cookie", ""} */
#define GRPC_MDELEM_SET_COOKIE_EMPTY_INDEX 55 * /

/* {"strict-transport-security", ""} */
#define GRPC_MDELEM_STRICT_TRANSPORT_SECURITY_EMPTY_INDEX 56

/* {"transfer-encoding", ""} */
#define GRPC_MDELEM_TRANSFER_ENCODING_EMPTY_INDEX 57

/* {"user-agent", ""} */
#define GRPC_MDELEM_USER_AGENT_EMPTY_INDEX 58

/* {"vary", ""} */
#define GRPC_MDELEM_VARY_EMPTY_INDEX 59

/* {"via", ""} */
#define GRPC_MDELEM_VIA_EMPTY_INDEX 60

/* {"www-authenticate", ""} */
#define GRPC_MDELEM_WWW_AUTHENTICATE_EMPTY_INDEX 61

/* End of static hpack table indices */

#define GRPC_IS_DEFAULT_HEADER_INDEX(idx) idx >= GRPC_MDELEM_AUTHORITY_EMPTY_INDEX 
  && idx <= GRPC_MDELEM_STATUS_500_INDEX

/* Forward declarations */
typedef struct grpc_mdelem grpc_mdelem;
#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_H */
