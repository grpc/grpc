/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/extension.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_EXTENSION_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_EXTENSION_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_core_v3_TypedExtensionConfig;
typedef struct envoy_config_core_v3_TypedExtensionConfig envoy_config_core_v3_TypedExtensionConfig;
extern const upb_MiniTable envoy_config_core_v3_TypedExtensionConfig_msginit;
struct google_protobuf_Any;
extern const upb_MiniTable google_protobuf_Any_msginit;



/* envoy.config.core.v3.TypedExtensionConfig */

UPB_INLINE envoy_config_core_v3_TypedExtensionConfig* envoy_config_core_v3_TypedExtensionConfig_new(upb_Arena* arena) {
  return (envoy_config_core_v3_TypedExtensionConfig*)_upb_Message_New(&envoy_config_core_v3_TypedExtensionConfig_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_TypedExtensionConfig* envoy_config_core_v3_TypedExtensionConfig_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_TypedExtensionConfig* ret = envoy_config_core_v3_TypedExtensionConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_TypedExtensionConfig_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_TypedExtensionConfig* envoy_config_core_v3_TypedExtensionConfig_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_TypedExtensionConfig* ret = envoy_config_core_v3_TypedExtensionConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_TypedExtensionConfig_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_TypedExtensionConfig_serialize(const envoy_config_core_v3_TypedExtensionConfig* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_TypedExtensionConfig_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_TypedExtensionConfig_serialize_ex(const envoy_config_core_v3_TypedExtensionConfig* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_TypedExtensionConfig_msginit, options, arena, len);
}
UPB_INLINE upb_StringView envoy_config_core_v3_TypedExtensionConfig_name(const envoy_config_core_v3_TypedExtensionConfig* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE bool envoy_config_core_v3_TypedExtensionConfig_has_typed_config(const envoy_config_core_v3_TypedExtensionConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Any* envoy_config_core_v3_TypedExtensionConfig_typed_config(const envoy_config_core_v3_TypedExtensionConfig* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_Any*);
}

UPB_INLINE void envoy_config_core_v3_TypedExtensionConfig_set_name(envoy_config_core_v3_TypedExtensionConfig *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void envoy_config_core_v3_TypedExtensionConfig_set_typed_config(envoy_config_core_v3_TypedExtensionConfig *msg, struct google_protobuf_Any* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_Any*) = value;
}
UPB_INLINE struct google_protobuf_Any* envoy_config_core_v3_TypedExtensionConfig_mutable_typed_config(envoy_config_core_v3_TypedExtensionConfig *msg, upb_Arena *arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)envoy_config_core_v3_TypedExtensionConfig_typed_config(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_Message_New(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_TypedExtensionConfig_set_typed_config(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File envoy_config_core_v3_extension_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_EXTENSION_PROTO_UPB_H_ */
