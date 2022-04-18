/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/v3/hash_policy.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_V3_HASH_POLICY_PROTO_UPB_H_
#define ENVOY_TYPE_V3_HASH_POLICY_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_type_v3_HashPolicy;
struct envoy_type_v3_HashPolicy_SourceIp;
struct envoy_type_v3_HashPolicy_FilterState;
typedef struct envoy_type_v3_HashPolicy envoy_type_v3_HashPolicy;
typedef struct envoy_type_v3_HashPolicy_SourceIp envoy_type_v3_HashPolicy_SourceIp;
typedef struct envoy_type_v3_HashPolicy_FilterState envoy_type_v3_HashPolicy_FilterState;
extern const upb_MiniTable envoy_type_v3_HashPolicy_msginit;
extern const upb_MiniTable envoy_type_v3_HashPolicy_SourceIp_msginit;
extern const upb_MiniTable envoy_type_v3_HashPolicy_FilterState_msginit;



/* envoy.type.v3.HashPolicy */

UPB_INLINE envoy_type_v3_HashPolicy* envoy_type_v3_HashPolicy_new(upb_Arena* arena) {
  return (envoy_type_v3_HashPolicy*)_upb_Message_New(&envoy_type_v3_HashPolicy_msginit, arena);
}
UPB_INLINE envoy_type_v3_HashPolicy* envoy_type_v3_HashPolicy_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_v3_HashPolicy* ret = envoy_type_v3_HashPolicy_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_v3_HashPolicy_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_v3_HashPolicy* envoy_type_v3_HashPolicy_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_v3_HashPolicy* ret = envoy_type_v3_HashPolicy_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_v3_HashPolicy_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_v3_HashPolicy_serialize(const envoy_type_v3_HashPolicy* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_type_v3_HashPolicy_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_type_v3_HashPolicy_serialize_ex(const envoy_type_v3_HashPolicy* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_type_v3_HashPolicy_msginit, options, arena, len);
}
typedef enum {
  envoy_type_v3_HashPolicy_policy_specifier_source_ip = 1,
  envoy_type_v3_HashPolicy_policy_specifier_filter_state = 2,
  envoy_type_v3_HashPolicy_policy_specifier_NOT_SET = 0
} envoy_type_v3_HashPolicy_policy_specifier_oneofcases;
UPB_INLINE envoy_type_v3_HashPolicy_policy_specifier_oneofcases envoy_type_v3_HashPolicy_policy_specifier_case(const envoy_type_v3_HashPolicy* msg) {
  return (envoy_type_v3_HashPolicy_policy_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}
UPB_INLINE bool envoy_type_v3_HashPolicy_has_source_ip(const envoy_type_v3_HashPolicy* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 1;
}
UPB_INLINE void envoy_type_v3_HashPolicy_clear_source_ip(const envoy_type_v3_HashPolicy* msg) {
  UPB_WRITE_ONEOF(msg, envoy_type_v3_HashPolicy_SourceIp*, UPB_SIZE(4, 8), 0, UPB_SIZE(0, 0), envoy_type_v3_HashPolicy_policy_specifier_NOT_SET);
}
UPB_INLINE const envoy_type_v3_HashPolicy_SourceIp* envoy_type_v3_HashPolicy_source_ip(const envoy_type_v3_HashPolicy* msg) {
  return UPB_READ_ONEOF(msg, const envoy_type_v3_HashPolicy_SourceIp*, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 1, NULL);
}
UPB_INLINE bool envoy_type_v3_HashPolicy_has_filter_state(const envoy_type_v3_HashPolicy* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 2;
}
UPB_INLINE void envoy_type_v3_HashPolicy_clear_filter_state(const envoy_type_v3_HashPolicy* msg) {
  UPB_WRITE_ONEOF(msg, envoy_type_v3_HashPolicy_FilterState*, UPB_SIZE(4, 8), 0, UPB_SIZE(0, 0), envoy_type_v3_HashPolicy_policy_specifier_NOT_SET);
}
UPB_INLINE const envoy_type_v3_HashPolicy_FilterState* envoy_type_v3_HashPolicy_filter_state(const envoy_type_v3_HashPolicy* msg) {
  return UPB_READ_ONEOF(msg, const envoy_type_v3_HashPolicy_FilterState*, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 2, NULL);
}

UPB_INLINE void envoy_type_v3_HashPolicy_set_source_ip(envoy_type_v3_HashPolicy *msg, envoy_type_v3_HashPolicy_SourceIp* value) {
  UPB_WRITE_ONEOF(msg, envoy_type_v3_HashPolicy_SourceIp*, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 1);
}
UPB_INLINE struct envoy_type_v3_HashPolicy_SourceIp* envoy_type_v3_HashPolicy_mutable_source_ip(envoy_type_v3_HashPolicy* msg, upb_Arena* arena) {
  struct envoy_type_v3_HashPolicy_SourceIp* sub = (struct envoy_type_v3_HashPolicy_SourceIp*)envoy_type_v3_HashPolicy_source_ip(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_v3_HashPolicy_SourceIp*)_upb_Message_New(&envoy_type_v3_HashPolicy_SourceIp_msginit, arena);
    if (!sub) return NULL;
    envoy_type_v3_HashPolicy_set_source_ip(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_v3_HashPolicy_set_filter_state(envoy_type_v3_HashPolicy *msg, envoy_type_v3_HashPolicy_FilterState* value) {
  UPB_WRITE_ONEOF(msg, envoy_type_v3_HashPolicy_FilterState*, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 2);
}
UPB_INLINE struct envoy_type_v3_HashPolicy_FilterState* envoy_type_v3_HashPolicy_mutable_filter_state(envoy_type_v3_HashPolicy* msg, upb_Arena* arena) {
  struct envoy_type_v3_HashPolicy_FilterState* sub = (struct envoy_type_v3_HashPolicy_FilterState*)envoy_type_v3_HashPolicy_filter_state(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_v3_HashPolicy_FilterState*)_upb_Message_New(&envoy_type_v3_HashPolicy_FilterState_msginit, arena);
    if (!sub) return NULL;
    envoy_type_v3_HashPolicy_set_filter_state(msg, sub);
  }
  return sub;
}

/* envoy.type.v3.HashPolicy.SourceIp */

UPB_INLINE envoy_type_v3_HashPolicy_SourceIp* envoy_type_v3_HashPolicy_SourceIp_new(upb_Arena* arena) {
  return (envoy_type_v3_HashPolicy_SourceIp*)_upb_Message_New(&envoy_type_v3_HashPolicy_SourceIp_msginit, arena);
}
UPB_INLINE envoy_type_v3_HashPolicy_SourceIp* envoy_type_v3_HashPolicy_SourceIp_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_v3_HashPolicy_SourceIp* ret = envoy_type_v3_HashPolicy_SourceIp_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_v3_HashPolicy_SourceIp_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_v3_HashPolicy_SourceIp* envoy_type_v3_HashPolicy_SourceIp_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_v3_HashPolicy_SourceIp* ret = envoy_type_v3_HashPolicy_SourceIp_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_v3_HashPolicy_SourceIp_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_v3_HashPolicy_SourceIp_serialize(const envoy_type_v3_HashPolicy_SourceIp* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_type_v3_HashPolicy_SourceIp_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_type_v3_HashPolicy_SourceIp_serialize_ex(const envoy_type_v3_HashPolicy_SourceIp* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_type_v3_HashPolicy_SourceIp_msginit, options, arena, len);
}


/* envoy.type.v3.HashPolicy.FilterState */

UPB_INLINE envoy_type_v3_HashPolicy_FilterState* envoy_type_v3_HashPolicy_FilterState_new(upb_Arena* arena) {
  return (envoy_type_v3_HashPolicy_FilterState*)_upb_Message_New(&envoy_type_v3_HashPolicy_FilterState_msginit, arena);
}
UPB_INLINE envoy_type_v3_HashPolicy_FilterState* envoy_type_v3_HashPolicy_FilterState_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_v3_HashPolicy_FilterState* ret = envoy_type_v3_HashPolicy_FilterState_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_v3_HashPolicy_FilterState_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_v3_HashPolicy_FilterState* envoy_type_v3_HashPolicy_FilterState_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_v3_HashPolicy_FilterState* ret = envoy_type_v3_HashPolicy_FilterState_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_v3_HashPolicy_FilterState_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_v3_HashPolicy_FilterState_serialize(const envoy_type_v3_HashPolicy_FilterState* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_type_v3_HashPolicy_FilterState_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_type_v3_HashPolicy_FilterState_serialize_ex(const envoy_type_v3_HashPolicy_FilterState* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_type_v3_HashPolicy_FilterState_msginit, options, arena, len);
}
UPB_INLINE void envoy_type_v3_HashPolicy_FilterState_clear_key(const envoy_type_v3_HashPolicy_FilterState* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_type_v3_HashPolicy_FilterState_key(const envoy_type_v3_HashPolicy_FilterState* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView);
}

UPB_INLINE void envoy_type_v3_HashPolicy_FilterState_set_key(envoy_type_v3_HashPolicy_FilterState *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = value;
}

extern const upb_MiniTable_File envoy_type_v3_hash_policy_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_V3_HASH_POLICY_PROTO_UPB_H_ */
