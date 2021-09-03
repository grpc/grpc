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
struct envoy_config_core_v3_ExtensionConfigSource;
typedef struct envoy_config_core_v3_TypedExtensionConfig envoy_config_core_v3_TypedExtensionConfig;
typedef struct envoy_config_core_v3_ExtensionConfigSource envoy_config_core_v3_ExtensionConfigSource;
extern const upb_msglayout envoy_config_core_v3_TypedExtensionConfig_msginit;
extern const upb_msglayout envoy_config_core_v3_ExtensionConfigSource_msginit;
struct envoy_config_core_v3_ConfigSource;
struct google_protobuf_Any;
extern const upb_msglayout envoy_config_core_v3_ConfigSource_msginit;
extern const upb_msglayout google_protobuf_Any_msginit;


/* envoy.config.core.v3.TypedExtensionConfig */

UPB_INLINE envoy_config_core_v3_TypedExtensionConfig *envoy_config_core_v3_TypedExtensionConfig_new(upb_arena *arena) {
  return (envoy_config_core_v3_TypedExtensionConfig *)_upb_msg_new(&envoy_config_core_v3_TypedExtensionConfig_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_TypedExtensionConfig *envoy_config_core_v3_TypedExtensionConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_TypedExtensionConfig *ret = envoy_config_core_v3_TypedExtensionConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_TypedExtensionConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_TypedExtensionConfig *envoy_config_core_v3_TypedExtensionConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_TypedExtensionConfig *ret = envoy_config_core_v3_TypedExtensionConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_TypedExtensionConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_TypedExtensionConfig_serialize(const envoy_config_core_v3_TypedExtensionConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_TypedExtensionConfig_msginit, arena, len);
}

UPB_INLINE upb_strview envoy_config_core_v3_TypedExtensionConfig_name(const envoy_config_core_v3_TypedExtensionConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview); }
UPB_INLINE bool envoy_config_core_v3_TypedExtensionConfig_has_typed_config(const envoy_config_core_v3_TypedExtensionConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Any* envoy_config_core_v3_TypedExtensionConfig_typed_config(const envoy_config_core_v3_TypedExtensionConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_Any*); }

UPB_INLINE void envoy_config_core_v3_TypedExtensionConfig_set_name(envoy_config_core_v3_TypedExtensionConfig *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview) = value;
}
UPB_INLINE void envoy_config_core_v3_TypedExtensionConfig_set_typed_config(envoy_config_core_v3_TypedExtensionConfig *msg, struct google_protobuf_Any* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_Any*) = value;
}
UPB_INLINE struct google_protobuf_Any* envoy_config_core_v3_TypedExtensionConfig_mutable_typed_config(envoy_config_core_v3_TypedExtensionConfig *msg, upb_arena *arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)envoy_config_core_v3_TypedExtensionConfig_typed_config(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_msg_new(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_TypedExtensionConfig_set_typed_config(msg, sub);
  }
  return sub;
}

/* envoy.config.core.v3.ExtensionConfigSource */

UPB_INLINE envoy_config_core_v3_ExtensionConfigSource *envoy_config_core_v3_ExtensionConfigSource_new(upb_arena *arena) {
  return (envoy_config_core_v3_ExtensionConfigSource *)_upb_msg_new(&envoy_config_core_v3_ExtensionConfigSource_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_ExtensionConfigSource *envoy_config_core_v3_ExtensionConfigSource_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_ExtensionConfigSource *ret = envoy_config_core_v3_ExtensionConfigSource_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_ExtensionConfigSource_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_ExtensionConfigSource *envoy_config_core_v3_ExtensionConfigSource_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_ExtensionConfigSource *ret = envoy_config_core_v3_ExtensionConfigSource_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_ExtensionConfigSource_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_ExtensionConfigSource_serialize(const envoy_config_core_v3_ExtensionConfigSource *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_ExtensionConfigSource_msginit, arena, len);
}

UPB_INLINE bool envoy_config_core_v3_ExtensionConfigSource_has_config_source(const envoy_config_core_v3_ExtensionConfigSource *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_ConfigSource* envoy_config_core_v3_ExtensionConfigSource_config_source(const envoy_config_core_v3_ExtensionConfigSource *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct envoy_config_core_v3_ConfigSource*); }
UPB_INLINE bool envoy_config_core_v3_ExtensionConfigSource_has_default_config(const envoy_config_core_v3_ExtensionConfigSource *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_Any* envoy_config_core_v3_ExtensionConfigSource_default_config(const envoy_config_core_v3_ExtensionConfigSource *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct google_protobuf_Any*); }
UPB_INLINE bool envoy_config_core_v3_ExtensionConfigSource_apply_default_config_without_warming(const envoy_config_core_v3_ExtensionConfigSource *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool); }
UPB_INLINE upb_strview const* envoy_config_core_v3_ExtensionConfigSource_type_urls(const envoy_config_core_v3_ExtensionConfigSource *msg, size_t *len) { return (upb_strview const*)_upb_array_accessor(msg, UPB_SIZE(12, 24), len); }

UPB_INLINE void envoy_config_core_v3_ExtensionConfigSource_set_config_source(envoy_config_core_v3_ExtensionConfigSource *msg, struct envoy_config_core_v3_ConfigSource* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct envoy_config_core_v3_ConfigSource*) = value;
}
UPB_INLINE struct envoy_config_core_v3_ConfigSource* envoy_config_core_v3_ExtensionConfigSource_mutable_config_source(envoy_config_core_v3_ExtensionConfigSource *msg, upb_arena *arena) {
  struct envoy_config_core_v3_ConfigSource* sub = (struct envoy_config_core_v3_ConfigSource*)envoy_config_core_v3_ExtensionConfigSource_config_source(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_ConfigSource*)_upb_msg_new(&envoy_config_core_v3_ConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ExtensionConfigSource_set_config_source(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ExtensionConfigSource_set_default_config(envoy_config_core_v3_ExtensionConfigSource *msg, struct google_protobuf_Any* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct google_protobuf_Any*) = value;
}
UPB_INLINE struct google_protobuf_Any* envoy_config_core_v3_ExtensionConfigSource_mutable_default_config(envoy_config_core_v3_ExtensionConfigSource *msg, upb_arena *arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)envoy_config_core_v3_ExtensionConfigSource_default_config(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_msg_new(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ExtensionConfigSource_set_default_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ExtensionConfigSource_set_apply_default_config_without_warming(envoy_config_core_v3_ExtensionConfigSource *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool) = value;
}
UPB_INLINE upb_strview* envoy_config_core_v3_ExtensionConfigSource_mutable_type_urls(envoy_config_core_v3_ExtensionConfigSource *msg, size_t *len) {
  return (upb_strview*)_upb_array_mutable_accessor(msg, UPB_SIZE(12, 24), len);
}
UPB_INLINE upb_strview* envoy_config_core_v3_ExtensionConfigSource_resize_type_urls(envoy_config_core_v3_ExtensionConfigSource *msg, size_t len, upb_arena *arena) {
  return (upb_strview*)_upb_array_resize_accessor2(msg, UPB_SIZE(12, 24), len, UPB_SIZE(3, 4), arena);
}
UPB_INLINE bool envoy_config_core_v3_ExtensionConfigSource_add_type_urls(envoy_config_core_v3_ExtensionConfigSource *msg, upb_strview val, upb_arena *arena) {
  return _upb_array_append_accessor2(msg, UPB_SIZE(12, 24), UPB_SIZE(3, 4), &val,
      arena);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_EXTENSION_PROTO_UPB_H_ */
