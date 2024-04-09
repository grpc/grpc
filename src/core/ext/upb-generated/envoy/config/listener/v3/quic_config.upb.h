/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/listener/v3/quic_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_LISTENER_V3_QUIC_CONFIG_PROTO_UPB_H_
#define ENVOY_CONFIG_LISTENER_V3_QUIC_CONFIG_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_config_listener_v3_QuicProtocolOptions envoy_config_listener_v3_QuicProtocolOptions;
extern const upb_MiniTable envoy_config_listener_v3_QuicProtocolOptions_msg_init;
struct envoy_config_core_v3_QuicProtocolOptions;
struct envoy_config_core_v3_RuntimeFeatureFlag;
struct envoy_config_core_v3_TypedExtensionConfig;
struct google_protobuf_Duration;
struct google_protobuf_UInt32Value;
extern const upb_MiniTable envoy_config_core_v3_QuicProtocolOptions_msg_init;
extern const upb_MiniTable envoy_config_core_v3_RuntimeFeatureFlag_msg_init;
extern const upb_MiniTable envoy_config_core_v3_TypedExtensionConfig_msg_init;
extern const upb_MiniTable google_protobuf_Duration_msg_init;
extern const upb_MiniTable google_protobuf_UInt32Value_msg_init;



/* envoy.config.listener.v3.QuicProtocolOptions */

UPB_INLINE envoy_config_listener_v3_QuicProtocolOptions* envoy_config_listener_v3_QuicProtocolOptions_new(upb_Arena* arena) {
  return (envoy_config_listener_v3_QuicProtocolOptions*)_upb_Message_New(&envoy_config_listener_v3_QuicProtocolOptions_msg_init, arena);
}
UPB_INLINE envoy_config_listener_v3_QuicProtocolOptions* envoy_config_listener_v3_QuicProtocolOptions_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_listener_v3_QuicProtocolOptions* ret = envoy_config_listener_v3_QuicProtocolOptions_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_listener_v3_QuicProtocolOptions_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_listener_v3_QuicProtocolOptions* envoy_config_listener_v3_QuicProtocolOptions_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_listener_v3_QuicProtocolOptions* ret = envoy_config_listener_v3_QuicProtocolOptions_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_listener_v3_QuicProtocolOptions_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_listener_v3_QuicProtocolOptions_serialize(const envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_listener_v3_QuicProtocolOptions_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_config_listener_v3_QuicProtocolOptions_serialize_ex(const envoy_config_listener_v3_QuicProtocolOptions* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_listener_v3_QuicProtocolOptions_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_quic_protocol_options(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_QuicProtocolOptions* envoy_config_listener_v3_QuicProtocolOptions_quic_protocol_options(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct envoy_config_core_v3_QuicProtocolOptions* default_val = NULL;
  const struct envoy_config_core_v3_QuicProtocolOptions* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_quic_protocol_options(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_idle_timeout(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_Duration* envoy_config_listener_v3_QuicProtocolOptions_idle_timeout(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct google_protobuf_Duration* default_val = NULL;
  const struct google_protobuf_Duration* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_idle_timeout(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_crypto_handshake_timeout(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 24), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_Duration* envoy_config_listener_v3_QuicProtocolOptions_crypto_handshake_timeout(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct google_protobuf_Duration* default_val = NULL;
  const struct google_protobuf_Duration* ret;
  const upb_MiniTableField field = {3, UPB_SIZE(12, 24), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_crypto_handshake_timeout(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 24), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_enabled(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(16, 32), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_RuntimeFeatureFlag* envoy_config_listener_v3_QuicProtocolOptions_enabled(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct envoy_config_core_v3_RuntimeFeatureFlag* default_val = NULL;
  const struct envoy_config_core_v3_RuntimeFeatureFlag* ret;
  const upb_MiniTableField field = {4, UPB_SIZE(16, 32), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_enabled(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(16, 32), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_packets_to_read_to_connection_count_ratio(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 40), 5, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_listener_v3_QuicProtocolOptions_packets_to_read_to_connection_count_ratio(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct google_protobuf_UInt32Value* default_val = NULL;
  const struct google_protobuf_UInt32Value* ret;
  const upb_MiniTableField field = {5, UPB_SIZE(20, 40), 5, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_packets_to_read_to_connection_count_ratio(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 40), 5, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_crypto_stream_config(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {6, UPB_SIZE(24, 48), 6, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_crypto_stream_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct envoy_config_core_v3_TypedExtensionConfig* default_val = NULL;
  const struct envoy_config_core_v3_TypedExtensionConfig* ret;
  const upb_MiniTableField field = {6, UPB_SIZE(24, 48), 6, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_crypto_stream_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {6, UPB_SIZE(24, 48), 6, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_proof_source_config(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {7, UPB_SIZE(28, 56), 7, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_proof_source_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct envoy_config_core_v3_TypedExtensionConfig* default_val = NULL;
  const struct envoy_config_core_v3_TypedExtensionConfig* ret;
  const upb_MiniTableField field = {7, UPB_SIZE(28, 56), 7, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_proof_source_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {7, UPB_SIZE(28, 56), 7, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_connection_id_generator_config(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {8, UPB_SIZE(32, 64), 8, 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_connection_id_generator_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct envoy_config_core_v3_TypedExtensionConfig* default_val = NULL;
  const struct envoy_config_core_v3_TypedExtensionConfig* ret;
  const upb_MiniTableField field = {8, UPB_SIZE(32, 64), 8, 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_connection_id_generator_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {8, UPB_SIZE(32, 64), 8, 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_clear_server_preferred_address_config(envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {9, UPB_SIZE(36, 72), 9, 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_server_preferred_address_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const struct envoy_config_core_v3_TypedExtensionConfig* default_val = NULL;
  const struct envoy_config_core_v3_TypedExtensionConfig* ret;
  const upb_MiniTableField field = {9, UPB_SIZE(36, 72), 9, 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_listener_v3_QuicProtocolOptions_has_server_preferred_address_config(const envoy_config_listener_v3_QuicProtocolOptions* msg) {
  const upb_MiniTableField field = {9, UPB_SIZE(36, 72), 9, 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_quic_protocol_options(envoy_config_listener_v3_QuicProtocolOptions *msg, struct envoy_config_core_v3_QuicProtocolOptions* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_QuicProtocolOptions* envoy_config_listener_v3_QuicProtocolOptions_mutable_quic_protocol_options(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_QuicProtocolOptions* sub = (struct envoy_config_core_v3_QuicProtocolOptions*)envoy_config_listener_v3_QuicProtocolOptions_quic_protocol_options(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_QuicProtocolOptions*)_upb_Message_New(&envoy_config_core_v3_QuicProtocolOptions_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_quic_protocol_options(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_idle_timeout(envoy_config_listener_v3_QuicProtocolOptions *msg, struct google_protobuf_Duration* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_listener_v3_QuicProtocolOptions_mutable_idle_timeout(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_listener_v3_QuicProtocolOptions_idle_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_idle_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_crypto_handshake_timeout(envoy_config_listener_v3_QuicProtocolOptions *msg, struct google_protobuf_Duration* value) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 24), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_listener_v3_QuicProtocolOptions_mutable_crypto_handshake_timeout(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_listener_v3_QuicProtocolOptions_crypto_handshake_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_crypto_handshake_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_enabled(envoy_config_listener_v3_QuicProtocolOptions *msg, struct envoy_config_core_v3_RuntimeFeatureFlag* value) {
  const upb_MiniTableField field = {4, UPB_SIZE(16, 32), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_RuntimeFeatureFlag* envoy_config_listener_v3_QuicProtocolOptions_mutable_enabled(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_RuntimeFeatureFlag* sub = (struct envoy_config_core_v3_RuntimeFeatureFlag*)envoy_config_listener_v3_QuicProtocolOptions_enabled(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_RuntimeFeatureFlag*)_upb_Message_New(&envoy_config_core_v3_RuntimeFeatureFlag_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_enabled(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_packets_to_read_to_connection_count_ratio(envoy_config_listener_v3_QuicProtocolOptions *msg, struct google_protobuf_UInt32Value* value) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 40), 5, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_listener_v3_QuicProtocolOptions_mutable_packets_to_read_to_connection_count_ratio(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_listener_v3_QuicProtocolOptions_packets_to_read_to_connection_count_ratio(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_Message_New(&google_protobuf_UInt32Value_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_packets_to_read_to_connection_count_ratio(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_crypto_stream_config(envoy_config_listener_v3_QuicProtocolOptions *msg, struct envoy_config_core_v3_TypedExtensionConfig* value) {
  const upb_MiniTableField field = {6, UPB_SIZE(24, 48), 6, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_mutable_crypto_stream_config(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_TypedExtensionConfig* sub = (struct envoy_config_core_v3_TypedExtensionConfig*)envoy_config_listener_v3_QuicProtocolOptions_crypto_stream_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_TypedExtensionConfig*)_upb_Message_New(&envoy_config_core_v3_TypedExtensionConfig_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_crypto_stream_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_proof_source_config(envoy_config_listener_v3_QuicProtocolOptions *msg, struct envoy_config_core_v3_TypedExtensionConfig* value) {
  const upb_MiniTableField field = {7, UPB_SIZE(28, 56), 7, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_mutable_proof_source_config(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_TypedExtensionConfig* sub = (struct envoy_config_core_v3_TypedExtensionConfig*)envoy_config_listener_v3_QuicProtocolOptions_proof_source_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_TypedExtensionConfig*)_upb_Message_New(&envoy_config_core_v3_TypedExtensionConfig_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_proof_source_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_connection_id_generator_config(envoy_config_listener_v3_QuicProtocolOptions *msg, struct envoy_config_core_v3_TypedExtensionConfig* value) {
  const upb_MiniTableField field = {8, UPB_SIZE(32, 64), 8, 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_mutable_connection_id_generator_config(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_TypedExtensionConfig* sub = (struct envoy_config_core_v3_TypedExtensionConfig*)envoy_config_listener_v3_QuicProtocolOptions_connection_id_generator_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_TypedExtensionConfig*)_upb_Message_New(&envoy_config_core_v3_TypedExtensionConfig_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_connection_id_generator_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_QuicProtocolOptions_set_server_preferred_address_config(envoy_config_listener_v3_QuicProtocolOptions *msg, struct envoy_config_core_v3_TypedExtensionConfig* value) {
  const upb_MiniTableField field = {9, UPB_SIZE(36, 72), 9, 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_listener_v3_QuicProtocolOptions_mutable_server_preferred_address_config(envoy_config_listener_v3_QuicProtocolOptions* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_TypedExtensionConfig* sub = (struct envoy_config_core_v3_TypedExtensionConfig*)envoy_config_listener_v3_QuicProtocolOptions_server_preferred_address_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_TypedExtensionConfig*)_upb_Message_New(&envoy_config_core_v3_TypedExtensionConfig_msg_init, arena);
    if (sub) envoy_config_listener_v3_QuicProtocolOptions_set_server_preferred_address_config(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile envoy_config_listener_v3_quic_config_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_CONFIG_LISTENER_V3_QUIC_CONFIG_PROTO_UPB_H_ */