/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/udp_socket_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_UDP_SOCKET_CONFIG_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_UDP_SOCKET_CONFIG_PROTO_UPB_H_

#include "upb/collections/array_internal.h"
#include "upb/collections/map_gencode_util.h"
#include "upb/message/accessors.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "upb/wire/decode.h"
#include "upb/wire/decode_fast.h"
#include "upb/wire/encode.h"

// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_config_core_v3_UdpSocketConfig envoy_config_core_v3_UdpSocketConfig;
extern const upb_MiniTable envoy_config_core_v3_UdpSocketConfig_msg_init;
struct google_protobuf_BoolValue;
struct google_protobuf_UInt64Value;
extern const upb_MiniTable google_protobuf_BoolValue_msg_init;
extern const upb_MiniTable google_protobuf_UInt64Value_msg_init;



/* envoy.config.core.v3.UdpSocketConfig */

UPB_INLINE envoy_config_core_v3_UdpSocketConfig* envoy_config_core_v3_UdpSocketConfig_new(upb_Arena* arena) {
  return (envoy_config_core_v3_UdpSocketConfig*)_upb_Message_New(&envoy_config_core_v3_UdpSocketConfig_msg_init, arena);
}
UPB_INLINE envoy_config_core_v3_UdpSocketConfig* envoy_config_core_v3_UdpSocketConfig_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_UdpSocketConfig* ret = envoy_config_core_v3_UdpSocketConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_UdpSocketConfig_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_UdpSocketConfig* envoy_config_core_v3_UdpSocketConfig_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_UdpSocketConfig* ret = envoy_config_core_v3_UdpSocketConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_UdpSocketConfig_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_UdpSocketConfig_serialize(const envoy_config_core_v3_UdpSocketConfig* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_core_v3_UdpSocketConfig_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_config_core_v3_UdpSocketConfig_serialize_ex(const envoy_config_core_v3_UdpSocketConfig* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_core_v3_UdpSocketConfig_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_config_core_v3_UdpSocketConfig_clear_max_rx_datagram_size(envoy_config_core_v3_UdpSocketConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_UInt64Value* envoy_config_core_v3_UdpSocketConfig_max_rx_datagram_size(const envoy_config_core_v3_UdpSocketConfig* msg) {
  const struct google_protobuf_UInt64Value* default_val = NULL;
  const struct google_protobuf_UInt64Value* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_core_v3_UdpSocketConfig_has_max_rx_datagram_size(const envoy_config_core_v3_UdpSocketConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_core_v3_UdpSocketConfig_clear_prefer_gro(envoy_config_core_v3_UdpSocketConfig* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_core_v3_UdpSocketConfig_prefer_gro(const envoy_config_core_v3_UdpSocketConfig* msg) {
  const struct google_protobuf_BoolValue* default_val = NULL;
  const struct google_protobuf_BoolValue* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_core_v3_UdpSocketConfig_has_prefer_gro(const envoy_config_core_v3_UdpSocketConfig* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void envoy_config_core_v3_UdpSocketConfig_set_max_rx_datagram_size(envoy_config_core_v3_UdpSocketConfig *msg, struct google_protobuf_UInt64Value* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_UInt64Value* envoy_config_core_v3_UdpSocketConfig_mutable_max_rx_datagram_size(envoy_config_core_v3_UdpSocketConfig* msg, upb_Arena* arena) {
  struct google_protobuf_UInt64Value* sub = (struct google_protobuf_UInt64Value*)envoy_config_core_v3_UdpSocketConfig_max_rx_datagram_size(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt64Value*)_upb_Message_New(&google_protobuf_UInt64Value_msg_init, arena);
    if (sub) envoy_config_core_v3_UdpSocketConfig_set_max_rx_datagram_size(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_UdpSocketConfig_set_prefer_gro(envoy_config_core_v3_UdpSocketConfig *msg, struct google_protobuf_BoolValue* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_core_v3_UdpSocketConfig_mutable_prefer_gro(envoy_config_core_v3_UdpSocketConfig* msg, upb_Arena* arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_core_v3_UdpSocketConfig_prefer_gro(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)_upb_Message_New(&google_protobuf_BoolValue_msg_init, arena);
    if (sub) envoy_config_core_v3_UdpSocketConfig_set_prefer_gro(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile envoy_config_core_v3_udp_socket_config_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_UDP_SOCKET_CONFIG_PROTO_UPB_H_ */
