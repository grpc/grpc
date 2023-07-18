/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     src/proto/grpc/health/v1/health.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef SRC_PROTO_GRPC_HEALTH_V1_HEALTH_PROTO_UPB_H_
#define SRC_PROTO_GRPC_HEALTH_V1_HEALTH_PROTO_UPB_H_

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

typedef struct grpc_health_v1_HealthCheckRequest grpc_health_v1_HealthCheckRequest;
typedef struct grpc_health_v1_HealthCheckResponse grpc_health_v1_HealthCheckResponse;
extern const upb_MiniTable grpc_health_v1_HealthCheckRequest_msg_init;
extern const upb_MiniTable grpc_health_v1_HealthCheckResponse_msg_init;

typedef enum {
  grpc_health_v1_HealthCheckResponse_UNKNOWN = 0,
  grpc_health_v1_HealthCheckResponse_SERVING = 1,
  grpc_health_v1_HealthCheckResponse_NOT_SERVING = 2,
  grpc_health_v1_HealthCheckResponse_SERVICE_UNKNOWN = 3
} grpc_health_v1_HealthCheckResponse_ServingStatus;



/* grpc.health.v1.HealthCheckRequest */

UPB_INLINE grpc_health_v1_HealthCheckRequest* grpc_health_v1_HealthCheckRequest_new(upb_Arena* arena) {
  return (grpc_health_v1_HealthCheckRequest*)_upb_Message_New(&grpc_health_v1_HealthCheckRequest_msg_init, arena);
}
UPB_INLINE grpc_health_v1_HealthCheckRequest* grpc_health_v1_HealthCheckRequest_parse(const char* buf, size_t size, upb_Arena* arena) {
  grpc_health_v1_HealthCheckRequest* ret = grpc_health_v1_HealthCheckRequest_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &grpc_health_v1_HealthCheckRequest_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE grpc_health_v1_HealthCheckRequest* grpc_health_v1_HealthCheckRequest_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  grpc_health_v1_HealthCheckRequest* ret = grpc_health_v1_HealthCheckRequest_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &grpc_health_v1_HealthCheckRequest_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* grpc_health_v1_HealthCheckRequest_serialize(const grpc_health_v1_HealthCheckRequest* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &grpc_health_v1_HealthCheckRequest_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* grpc_health_v1_HealthCheckRequest_serialize_ex(const grpc_health_v1_HealthCheckRequest* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &grpc_health_v1_HealthCheckRequest_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void grpc_health_v1_HealthCheckRequest_clear_service(grpc_health_v1_HealthCheckRequest* msg) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView grpc_health_v1_HealthCheckRequest_service(const grpc_health_v1_HealthCheckRequest* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE void grpc_health_v1_HealthCheckRequest_set_service(grpc_health_v1_HealthCheckRequest *msg, upb_StringView value) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

/* grpc.health.v1.HealthCheckResponse */

UPB_INLINE grpc_health_v1_HealthCheckResponse* grpc_health_v1_HealthCheckResponse_new(upb_Arena* arena) {
  return (grpc_health_v1_HealthCheckResponse*)_upb_Message_New(&grpc_health_v1_HealthCheckResponse_msg_init, arena);
}
UPB_INLINE grpc_health_v1_HealthCheckResponse* grpc_health_v1_HealthCheckResponse_parse(const char* buf, size_t size, upb_Arena* arena) {
  grpc_health_v1_HealthCheckResponse* ret = grpc_health_v1_HealthCheckResponse_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &grpc_health_v1_HealthCheckResponse_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE grpc_health_v1_HealthCheckResponse* grpc_health_v1_HealthCheckResponse_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  grpc_health_v1_HealthCheckResponse* ret = grpc_health_v1_HealthCheckResponse_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &grpc_health_v1_HealthCheckResponse_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* grpc_health_v1_HealthCheckResponse_serialize(const grpc_health_v1_HealthCheckResponse* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &grpc_health_v1_HealthCheckResponse_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* grpc_health_v1_HealthCheckResponse_serialize_ex(const grpc_health_v1_HealthCheckResponse* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &grpc_health_v1_HealthCheckResponse_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void grpc_health_v1_HealthCheckResponse_clear_status(grpc_health_v1_HealthCheckResponse* msg) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t grpc_health_v1_HealthCheckResponse_status(const grpc_health_v1_HealthCheckResponse* msg) {
  int32_t default_val = 0;
  int32_t ret;
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE void grpc_health_v1_HealthCheckResponse_set_status(grpc_health_v1_HealthCheckResponse *msg, int32_t value) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

extern const upb_MiniTableFile src_proto_grpc_health_v1_health_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* SRC_PROTO_GRPC_HEALTH_V1_HEALTH_PROTO_UPB_H_ */
