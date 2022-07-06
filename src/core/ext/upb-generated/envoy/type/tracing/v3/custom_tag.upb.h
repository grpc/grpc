/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/tracing/v3/custom_tag.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_TRACING_V3_CUSTOM_TAG_PROTO_UPB_H_
#define ENVOY_TYPE_TRACING_V3_CUSTOM_TAG_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_type_tracing_v3_CustomTag;
struct envoy_type_tracing_v3_CustomTag_Literal;
struct envoy_type_tracing_v3_CustomTag_Environment;
struct envoy_type_tracing_v3_CustomTag_Header;
struct envoy_type_tracing_v3_CustomTag_Metadata;
typedef struct envoy_type_tracing_v3_CustomTag envoy_type_tracing_v3_CustomTag;
typedef struct envoy_type_tracing_v3_CustomTag_Literal envoy_type_tracing_v3_CustomTag_Literal;
typedef struct envoy_type_tracing_v3_CustomTag_Environment envoy_type_tracing_v3_CustomTag_Environment;
typedef struct envoy_type_tracing_v3_CustomTag_Header envoy_type_tracing_v3_CustomTag_Header;
typedef struct envoy_type_tracing_v3_CustomTag_Metadata envoy_type_tracing_v3_CustomTag_Metadata;
extern const upb_MiniTable envoy_type_tracing_v3_CustomTag_msginit;
extern const upb_MiniTable envoy_type_tracing_v3_CustomTag_Literal_msginit;
extern const upb_MiniTable envoy_type_tracing_v3_CustomTag_Environment_msginit;
extern const upb_MiniTable envoy_type_tracing_v3_CustomTag_Header_msginit;
extern const upb_MiniTable envoy_type_tracing_v3_CustomTag_Metadata_msginit;
struct envoy_type_metadata_v3_MetadataKey;
struct envoy_type_metadata_v3_MetadataKind;
extern const upb_MiniTable envoy_type_metadata_v3_MetadataKey_msginit;
extern const upb_MiniTable envoy_type_metadata_v3_MetadataKind_msginit;



/* envoy.type.tracing.v3.CustomTag */

UPB_INLINE envoy_type_tracing_v3_CustomTag* envoy_type_tracing_v3_CustomTag_new(upb_Arena* arena) {
  return (envoy_type_tracing_v3_CustomTag*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_msginit, arena);
}
UPB_INLINE envoy_type_tracing_v3_CustomTag* envoy_type_tracing_v3_CustomTag_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag* ret = envoy_type_tracing_v3_CustomTag_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_tracing_v3_CustomTag* envoy_type_tracing_v3_CustomTag_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag* ret = envoy_type_tracing_v3_CustomTag_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_serialize(const envoy_type_tracing_v3_CustomTag* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_serialize_ex(const envoy_type_tracing_v3_CustomTag* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_msginit, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  envoy_type_tracing_v3_CustomTag_type_literal = 2,
  envoy_type_tracing_v3_CustomTag_type_environment = 3,
  envoy_type_tracing_v3_CustomTag_type_request_header = 4,
  envoy_type_tracing_v3_CustomTag_type_metadata = 5,
  envoy_type_tracing_v3_CustomTag_type_NOT_SET = 0
} envoy_type_tracing_v3_CustomTag_type_oneofcases;
UPB_INLINE envoy_type_tracing_v3_CustomTag_type_oneofcases envoy_type_tracing_v3_CustomTag_type_case(const envoy_type_tracing_v3_CustomTag* msg) {
  return (envoy_type_tracing_v3_CustomTag_type_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_clear_tag(const envoy_type_tracing_v3_CustomTag* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_tag(const envoy_type_tracing_v3_CustomTag* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE bool envoy_type_tracing_v3_CustomTag_has_literal(const envoy_type_tracing_v3_CustomTag* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 2;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_clear_literal(const envoy_type_tracing_v3_CustomTag* msg) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Literal*, UPB_SIZE(12, 24), 0, UPB_SIZE(0, 0), envoy_type_tracing_v3_CustomTag_type_NOT_SET);
}
UPB_INLINE const envoy_type_tracing_v3_CustomTag_Literal* envoy_type_tracing_v3_CustomTag_literal(const envoy_type_tracing_v3_CustomTag* msg) {
  return UPB_READ_ONEOF(msg, const envoy_type_tracing_v3_CustomTag_Literal*, UPB_SIZE(12, 24), UPB_SIZE(0, 0), 2, NULL);
}
UPB_INLINE bool envoy_type_tracing_v3_CustomTag_has_environment(const envoy_type_tracing_v3_CustomTag* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 3;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_clear_environment(const envoy_type_tracing_v3_CustomTag* msg) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Environment*, UPB_SIZE(12, 24), 0, UPB_SIZE(0, 0), envoy_type_tracing_v3_CustomTag_type_NOT_SET);
}
UPB_INLINE const envoy_type_tracing_v3_CustomTag_Environment* envoy_type_tracing_v3_CustomTag_environment(const envoy_type_tracing_v3_CustomTag* msg) {
  return UPB_READ_ONEOF(msg, const envoy_type_tracing_v3_CustomTag_Environment*, UPB_SIZE(12, 24), UPB_SIZE(0, 0), 3, NULL);
}
UPB_INLINE bool envoy_type_tracing_v3_CustomTag_has_request_header(const envoy_type_tracing_v3_CustomTag* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 4;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_clear_request_header(const envoy_type_tracing_v3_CustomTag* msg) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Header*, UPB_SIZE(12, 24), 0, UPB_SIZE(0, 0), envoy_type_tracing_v3_CustomTag_type_NOT_SET);
}
UPB_INLINE const envoy_type_tracing_v3_CustomTag_Header* envoy_type_tracing_v3_CustomTag_request_header(const envoy_type_tracing_v3_CustomTag* msg) {
  return UPB_READ_ONEOF(msg, const envoy_type_tracing_v3_CustomTag_Header*, UPB_SIZE(12, 24), UPB_SIZE(0, 0), 4, NULL);
}
UPB_INLINE bool envoy_type_tracing_v3_CustomTag_has_metadata(const envoy_type_tracing_v3_CustomTag* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 5;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_clear_metadata(const envoy_type_tracing_v3_CustomTag* msg) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Metadata*, UPB_SIZE(12, 24), 0, UPB_SIZE(0, 0), envoy_type_tracing_v3_CustomTag_type_NOT_SET);
}
UPB_INLINE const envoy_type_tracing_v3_CustomTag_Metadata* envoy_type_tracing_v3_CustomTag_metadata(const envoy_type_tracing_v3_CustomTag* msg) {
  return UPB_READ_ONEOF(msg, const envoy_type_tracing_v3_CustomTag_Metadata*, UPB_SIZE(12, 24), UPB_SIZE(0, 0), 5, NULL);
}

UPB_INLINE void envoy_type_tracing_v3_CustomTag_set_tag(envoy_type_tracing_v3_CustomTag *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_set_literal(envoy_type_tracing_v3_CustomTag *msg, envoy_type_tracing_v3_CustomTag_Literal* value) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Literal*, UPB_SIZE(12, 24), value, UPB_SIZE(0, 0), 2);
}
UPB_INLINE struct envoy_type_tracing_v3_CustomTag_Literal* envoy_type_tracing_v3_CustomTag_mutable_literal(envoy_type_tracing_v3_CustomTag* msg, upb_Arena* arena) {
  struct envoy_type_tracing_v3_CustomTag_Literal* sub = (struct envoy_type_tracing_v3_CustomTag_Literal*)envoy_type_tracing_v3_CustomTag_literal(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_tracing_v3_CustomTag_Literal*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Literal_msginit, arena);
    if (!sub) return NULL;
    envoy_type_tracing_v3_CustomTag_set_literal(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_set_environment(envoy_type_tracing_v3_CustomTag *msg, envoy_type_tracing_v3_CustomTag_Environment* value) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Environment*, UPB_SIZE(12, 24), value, UPB_SIZE(0, 0), 3);
}
UPB_INLINE struct envoy_type_tracing_v3_CustomTag_Environment* envoy_type_tracing_v3_CustomTag_mutable_environment(envoy_type_tracing_v3_CustomTag* msg, upb_Arena* arena) {
  struct envoy_type_tracing_v3_CustomTag_Environment* sub = (struct envoy_type_tracing_v3_CustomTag_Environment*)envoy_type_tracing_v3_CustomTag_environment(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_tracing_v3_CustomTag_Environment*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Environment_msginit, arena);
    if (!sub) return NULL;
    envoy_type_tracing_v3_CustomTag_set_environment(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_set_request_header(envoy_type_tracing_v3_CustomTag *msg, envoy_type_tracing_v3_CustomTag_Header* value) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Header*, UPB_SIZE(12, 24), value, UPB_SIZE(0, 0), 4);
}
UPB_INLINE struct envoy_type_tracing_v3_CustomTag_Header* envoy_type_tracing_v3_CustomTag_mutable_request_header(envoy_type_tracing_v3_CustomTag* msg, upb_Arena* arena) {
  struct envoy_type_tracing_v3_CustomTag_Header* sub = (struct envoy_type_tracing_v3_CustomTag_Header*)envoy_type_tracing_v3_CustomTag_request_header(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_tracing_v3_CustomTag_Header*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Header_msginit, arena);
    if (!sub) return NULL;
    envoy_type_tracing_v3_CustomTag_set_request_header(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_set_metadata(envoy_type_tracing_v3_CustomTag *msg, envoy_type_tracing_v3_CustomTag_Metadata* value) {
  UPB_WRITE_ONEOF(msg, envoy_type_tracing_v3_CustomTag_Metadata*, UPB_SIZE(12, 24), value, UPB_SIZE(0, 0), 5);
}
UPB_INLINE struct envoy_type_tracing_v3_CustomTag_Metadata* envoy_type_tracing_v3_CustomTag_mutable_metadata(envoy_type_tracing_v3_CustomTag* msg, upb_Arena* arena) {
  struct envoy_type_tracing_v3_CustomTag_Metadata* sub = (struct envoy_type_tracing_v3_CustomTag_Metadata*)envoy_type_tracing_v3_CustomTag_metadata(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_tracing_v3_CustomTag_Metadata*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Metadata_msginit, arena);
    if (!sub) return NULL;
    envoy_type_tracing_v3_CustomTag_set_metadata(msg, sub);
  }
  return sub;
}

/* envoy.type.tracing.v3.CustomTag.Literal */

UPB_INLINE envoy_type_tracing_v3_CustomTag_Literal* envoy_type_tracing_v3_CustomTag_Literal_new(upb_Arena* arena) {
  return (envoy_type_tracing_v3_CustomTag_Literal*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Literal_msginit, arena);
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Literal* envoy_type_tracing_v3_CustomTag_Literal_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Literal* ret = envoy_type_tracing_v3_CustomTag_Literal_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Literal_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Literal* envoy_type_tracing_v3_CustomTag_Literal_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Literal* ret = envoy_type_tracing_v3_CustomTag_Literal_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Literal_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Literal_serialize(const envoy_type_tracing_v3_CustomTag_Literal* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Literal_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Literal_serialize_ex(const envoy_type_tracing_v3_CustomTag_Literal* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Literal_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Literal_clear_value(const envoy_type_tracing_v3_CustomTag_Literal* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_Literal_value(const envoy_type_tracing_v3_CustomTag_Literal* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView);
}

UPB_INLINE void envoy_type_tracing_v3_CustomTag_Literal_set_value(envoy_type_tracing_v3_CustomTag_Literal *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = value;
}

/* envoy.type.tracing.v3.CustomTag.Environment */

UPB_INLINE envoy_type_tracing_v3_CustomTag_Environment* envoy_type_tracing_v3_CustomTag_Environment_new(upb_Arena* arena) {
  return (envoy_type_tracing_v3_CustomTag_Environment*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Environment_msginit, arena);
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Environment* envoy_type_tracing_v3_CustomTag_Environment_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Environment* ret = envoy_type_tracing_v3_CustomTag_Environment_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Environment_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Environment* envoy_type_tracing_v3_CustomTag_Environment_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Environment* ret = envoy_type_tracing_v3_CustomTag_Environment_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Environment_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Environment_serialize(const envoy_type_tracing_v3_CustomTag_Environment* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Environment_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Environment_serialize_ex(const envoy_type_tracing_v3_CustomTag_Environment* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Environment_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Environment_clear_name(const envoy_type_tracing_v3_CustomTag_Environment* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_Environment_name(const envoy_type_tracing_v3_CustomTag_Environment* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView);
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Environment_clear_default_value(const envoy_type_tracing_v3_CustomTag_Environment* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_Environment_default_value(const envoy_type_tracing_v3_CustomTag_Environment* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView);
}

UPB_INLINE void envoy_type_tracing_v3_CustomTag_Environment_set_name(envoy_type_tracing_v3_CustomTag_Environment *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = value;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Environment_set_default_value(envoy_type_tracing_v3_CustomTag_Environment *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView) = value;
}

/* envoy.type.tracing.v3.CustomTag.Header */

UPB_INLINE envoy_type_tracing_v3_CustomTag_Header* envoy_type_tracing_v3_CustomTag_Header_new(upb_Arena* arena) {
  return (envoy_type_tracing_v3_CustomTag_Header*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Header_msginit, arena);
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Header* envoy_type_tracing_v3_CustomTag_Header_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Header* ret = envoy_type_tracing_v3_CustomTag_Header_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Header_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Header* envoy_type_tracing_v3_CustomTag_Header_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Header* ret = envoy_type_tracing_v3_CustomTag_Header_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Header_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Header_serialize(const envoy_type_tracing_v3_CustomTag_Header* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Header_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Header_serialize_ex(const envoy_type_tracing_v3_CustomTag_Header* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Header_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Header_clear_name(const envoy_type_tracing_v3_CustomTag_Header* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_Header_name(const envoy_type_tracing_v3_CustomTag_Header* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView);
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Header_clear_default_value(const envoy_type_tracing_v3_CustomTag_Header* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_Header_default_value(const envoy_type_tracing_v3_CustomTag_Header* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView);
}

UPB_INLINE void envoy_type_tracing_v3_CustomTag_Header_set_name(envoy_type_tracing_v3_CustomTag_Header *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = value;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Header_set_default_value(envoy_type_tracing_v3_CustomTag_Header *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView) = value;
}

/* envoy.type.tracing.v3.CustomTag.Metadata */

UPB_INLINE envoy_type_tracing_v3_CustomTag_Metadata* envoy_type_tracing_v3_CustomTag_Metadata_new(upb_Arena* arena) {
  return (envoy_type_tracing_v3_CustomTag_Metadata*)_upb_Message_New(&envoy_type_tracing_v3_CustomTag_Metadata_msginit, arena);
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Metadata* envoy_type_tracing_v3_CustomTag_Metadata_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Metadata* ret = envoy_type_tracing_v3_CustomTag_Metadata_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Metadata_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_tracing_v3_CustomTag_Metadata* envoy_type_tracing_v3_CustomTag_Metadata_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_tracing_v3_CustomTag_Metadata* ret = envoy_type_tracing_v3_CustomTag_Metadata_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_tracing_v3_CustomTag_Metadata_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Metadata_serialize(const envoy_type_tracing_v3_CustomTag_Metadata* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Metadata_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_tracing_v3_CustomTag_Metadata_serialize_ex(const envoy_type_tracing_v3_CustomTag_Metadata* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_tracing_v3_CustomTag_Metadata_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool envoy_type_tracing_v3_CustomTag_Metadata_has_kind(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Metadata_clear_kind(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const upb_Message*) = NULL;
}
UPB_INLINE const struct envoy_type_metadata_v3_MetadataKind* envoy_type_tracing_v3_CustomTag_Metadata_kind(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct envoy_type_metadata_v3_MetadataKind*);
}
UPB_INLINE bool envoy_type_tracing_v3_CustomTag_Metadata_has_metadata_key(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  return _upb_hasbit(msg, 2);
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Metadata_clear_metadata_key(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const upb_Message*) = NULL;
}
UPB_INLINE const struct envoy_type_metadata_v3_MetadataKey* envoy_type_tracing_v3_CustomTag_Metadata_metadata_key(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct envoy_type_metadata_v3_MetadataKey*);
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Metadata_clear_default_value(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_tracing_v3_CustomTag_Metadata_default_value(const envoy_type_tracing_v3_CustomTag_Metadata* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView);
}

UPB_INLINE void envoy_type_tracing_v3_CustomTag_Metadata_set_kind(envoy_type_tracing_v3_CustomTag_Metadata *msg, struct envoy_type_metadata_v3_MetadataKind* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct envoy_type_metadata_v3_MetadataKind*) = value;
}
UPB_INLINE struct envoy_type_metadata_v3_MetadataKind* envoy_type_tracing_v3_CustomTag_Metadata_mutable_kind(envoy_type_tracing_v3_CustomTag_Metadata* msg, upb_Arena* arena) {
  struct envoy_type_metadata_v3_MetadataKind* sub = (struct envoy_type_metadata_v3_MetadataKind*)envoy_type_tracing_v3_CustomTag_Metadata_kind(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_metadata_v3_MetadataKind*)_upb_Message_New(&envoy_type_metadata_v3_MetadataKind_msginit, arena);
    if (!sub) return NULL;
    envoy_type_tracing_v3_CustomTag_Metadata_set_kind(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Metadata_set_metadata_key(envoy_type_tracing_v3_CustomTag_Metadata *msg, struct envoy_type_metadata_v3_MetadataKey* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct envoy_type_metadata_v3_MetadataKey*) = value;
}
UPB_INLINE struct envoy_type_metadata_v3_MetadataKey* envoy_type_tracing_v3_CustomTag_Metadata_mutable_metadata_key(envoy_type_tracing_v3_CustomTag_Metadata* msg, upb_Arena* arena) {
  struct envoy_type_metadata_v3_MetadataKey* sub = (struct envoy_type_metadata_v3_MetadataKey*)envoy_type_tracing_v3_CustomTag_Metadata_metadata_key(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_metadata_v3_MetadataKey*)_upb_Message_New(&envoy_type_metadata_v3_MetadataKey_msginit, arena);
    if (!sub) return NULL;
    envoy_type_tracing_v3_CustomTag_Metadata_set_metadata_key(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_tracing_v3_CustomTag_Metadata_set_default_value(envoy_type_tracing_v3_CustomTag_Metadata *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = value;
}

extern const upb_MiniTable_File envoy_type_tracing_v3_custom_tag_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_TRACING_V3_CUSTOM_TAG_PROTO_UPB_H_ */
