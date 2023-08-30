/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/event_service_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_EVENT_SERVICE_CONFIG_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_EVENT_SERVICE_CONFIG_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_config_core_v3_EventServiceConfig envoy_config_core_v3_EventServiceConfig;
extern const upb_MiniTable envoy_config_core_v3_EventServiceConfig_msg_init;
struct envoy_config_core_v3_GrpcService;
extern const upb_MiniTable envoy_config_core_v3_GrpcService_msg_init;



/* envoy.config.core.v3.EventServiceConfig */

UPB_INLINE envoy_config_core_v3_EventServiceConfig* envoy_config_core_v3_EventServiceConfig_new(upb_Arena* arena) {
  return (envoy_config_core_v3_EventServiceConfig*)_upb_Message_New(&envoy_config_core_v3_EventServiceConfig_msg_init, arena);
}
UPB_INLINE envoy_config_core_v3_EventServiceConfig* envoy_config_core_v3_EventServiceConfig_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_EventServiceConfig* ret = envoy_config_core_v3_EventServiceConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_EventServiceConfig_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_EventServiceConfig* envoy_config_core_v3_EventServiceConfig_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_EventServiceConfig* ret = envoy_config_core_v3_EventServiceConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_EventServiceConfig_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_EventServiceConfig_serialize(const envoy_config_core_v3_EventServiceConfig* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_core_v3_EventServiceConfig_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_config_core_v3_EventServiceConfig_serialize_ex(const envoy_config_core_v3_EventServiceConfig* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_core_v3_EventServiceConfig_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  envoy_config_core_v3_EventServiceConfig_config_source_specifier_grpc_service = 1,
  envoy_config_core_v3_EventServiceConfig_config_source_specifier_NOT_SET = 0
} envoy_config_core_v3_EventServiceConfig_config_source_specifier_oneofcases;
UPB_INLINE envoy_config_core_v3_EventServiceConfig_config_source_specifier_oneofcases envoy_config_core_v3_EventServiceConfig_config_source_specifier_case(const envoy_config_core_v3_EventServiceConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (envoy_config_core_v3_EventServiceConfig_config_source_specifier_oneofcases)upb_Message_WhichOneofFieldNumber(msg, &field);
}
UPB_INLINE void envoy_config_core_v3_EventServiceConfig_clear_grpc_service(envoy_config_core_v3_EventServiceConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_core_v3_GrpcService* envoy_config_core_v3_EventServiceConfig_grpc_service(const envoy_config_core_v3_EventServiceConfig* msg) {
  const struct envoy_config_core_v3_GrpcService* default_val = NULL;
  const struct envoy_config_core_v3_GrpcService* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_core_v3_EventServiceConfig_has_grpc_service(const envoy_config_core_v3_EventServiceConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void envoy_config_core_v3_EventServiceConfig_set_grpc_service(envoy_config_core_v3_EventServiceConfig *msg, struct envoy_config_core_v3_GrpcService* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_core_v3_GrpcService* envoy_config_core_v3_EventServiceConfig_mutable_grpc_service(envoy_config_core_v3_EventServiceConfig* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_GrpcService* sub = (struct envoy_config_core_v3_GrpcService*)envoy_config_core_v3_EventServiceConfig_grpc_service(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_GrpcService*)_upb_Message_New(&envoy_config_core_v3_GrpcService_msg_init, arena);
    if (sub) envoy_config_core_v3_EventServiceConfig_set_grpc_service(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile envoy_config_core_v3_event_service_config_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_EVENT_SERVICE_CONFIG_PROTO_UPB_H_ */
