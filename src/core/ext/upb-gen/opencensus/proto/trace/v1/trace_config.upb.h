/* This file was generated by upb_generator from the input file:
 *
 *     opencensus/proto/trace/v1/trace_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef OPENCENSUS_PROTO_TRACE_V1_TRACE_CONFIG_PROTO_UPB_H_
#define OPENCENSUS_PROTO_TRACE_V1_TRACE_CONFIG_PROTO_UPB_H_

#include "upb/generated_code_support.h"

#include "opencensus/proto/trace/v1/trace_config.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct opencensus_proto_trace_v1_TraceConfig { upb_Message UPB_PRIVATE(base); } opencensus_proto_trace_v1_TraceConfig;
typedef struct opencensus_proto_trace_v1_ProbabilitySampler { upb_Message UPB_PRIVATE(base); } opencensus_proto_trace_v1_ProbabilitySampler;
typedef struct opencensus_proto_trace_v1_ConstantSampler { upb_Message UPB_PRIVATE(base); } opencensus_proto_trace_v1_ConstantSampler;
typedef struct opencensus_proto_trace_v1_RateLimitingSampler { upb_Message UPB_PRIVATE(base); } opencensus_proto_trace_v1_RateLimitingSampler;

typedef enum {
  opencensus_proto_trace_v1_ConstantSampler_ALWAYS_OFF = 0,
  opencensus_proto_trace_v1_ConstantSampler_ALWAYS_ON = 1,
  opencensus_proto_trace_v1_ConstantSampler_ALWAYS_PARENT = 2
} opencensus_proto_trace_v1_ConstantSampler_ConstantDecision;



/* opencensus.proto.trace.v1.TraceConfig */

UPB_INLINE opencensus_proto_trace_v1_TraceConfig* opencensus_proto_trace_v1_TraceConfig_new(upb_Arena* arena) {
  return (opencensus_proto_trace_v1_TraceConfig*)_upb_Message_New(&opencensus__proto__trace__v1__TraceConfig_msg_init, arena);
}
UPB_INLINE opencensus_proto_trace_v1_TraceConfig* opencensus_proto_trace_v1_TraceConfig_parse(const char* buf, size_t size, upb_Arena* arena) {
  opencensus_proto_trace_v1_TraceConfig* ret = opencensus_proto_trace_v1_TraceConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__TraceConfig_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE opencensus_proto_trace_v1_TraceConfig* opencensus_proto_trace_v1_TraceConfig_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  opencensus_proto_trace_v1_TraceConfig* ret = opencensus_proto_trace_v1_TraceConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__TraceConfig_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* opencensus_proto_trace_v1_TraceConfig_serialize(const opencensus_proto_trace_v1_TraceConfig* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__TraceConfig_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* opencensus_proto_trace_v1_TraceConfig_serialize_ex(const opencensus_proto_trace_v1_TraceConfig* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__TraceConfig_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  opencensus_proto_trace_v1_TraceConfig_sampler_probability_sampler = 1,
  opencensus_proto_trace_v1_TraceConfig_sampler_constant_sampler = 2,
  opencensus_proto_trace_v1_TraceConfig_sampler_rate_limiting_sampler = 3,
  opencensus_proto_trace_v1_TraceConfig_sampler_NOT_SET = 0
} opencensus_proto_trace_v1_TraceConfig_sampler_oneofcases;
UPB_INLINE opencensus_proto_trace_v1_TraceConfig_sampler_oneofcases opencensus_proto_trace_v1_TraceConfig_sampler_case(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 48), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (opencensus_proto_trace_v1_TraceConfig_sampler_oneofcases)upb_Message_WhichOneofFieldNumber(
      UPB_UPCAST(msg), &field);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_probability_sampler(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 48), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const opencensus_proto_trace_v1_ProbabilitySampler* opencensus_proto_trace_v1_TraceConfig_probability_sampler(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const opencensus_proto_trace_v1_ProbabilitySampler* default_val = NULL;
  const opencensus_proto_trace_v1_ProbabilitySampler* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(12, 48), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&opencensus__proto__trace__v1__ProbabilitySampler_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool opencensus_proto_trace_v1_TraceConfig_has_probability_sampler(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 48), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_constant_sampler(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 48), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const opencensus_proto_trace_v1_ConstantSampler* opencensus_proto_trace_v1_TraceConfig_constant_sampler(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const opencensus_proto_trace_v1_ConstantSampler* default_val = NULL;
  const opencensus_proto_trace_v1_ConstantSampler* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(12, 48), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&opencensus__proto__trace__v1__ConstantSampler_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool opencensus_proto_trace_v1_TraceConfig_has_constant_sampler(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 48), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_rate_limiting_sampler(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 48), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const opencensus_proto_trace_v1_RateLimitingSampler* opencensus_proto_trace_v1_TraceConfig_rate_limiting_sampler(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const opencensus_proto_trace_v1_RateLimitingSampler* default_val = NULL;
  const opencensus_proto_trace_v1_RateLimitingSampler* ret;
  const upb_MiniTableField field = {3, UPB_SIZE(12, 48), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&opencensus__proto__trace__v1__RateLimitingSampler_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool opencensus_proto_trace_v1_TraceConfig_has_rate_limiting_sampler(const opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 48), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_max_number_of_attributes(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {4, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE int64_t opencensus_proto_trace_v1_TraceConfig_max_number_of_attributes(const opencensus_proto_trace_v1_TraceConfig* msg) {
  int64_t default_val = (int64_t)0ll;
  int64_t ret;
  const upb_MiniTableField field = {4, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_max_number_of_annotations(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {5, 24, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE int64_t opencensus_proto_trace_v1_TraceConfig_max_number_of_annotations(const opencensus_proto_trace_v1_TraceConfig* msg) {
  int64_t default_val = (int64_t)0ll;
  int64_t ret;
  const upb_MiniTableField field = {5, 24, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_max_number_of_message_events(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {6, 32, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE int64_t opencensus_proto_trace_v1_TraceConfig_max_number_of_message_events(const opencensus_proto_trace_v1_TraceConfig* msg) {
  int64_t default_val = (int64_t)0ll;
  int64_t ret;
  const upb_MiniTableField field = {6, 32, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_clear_max_number_of_links(opencensus_proto_trace_v1_TraceConfig* msg) {
  const upb_MiniTableField field = {7, 40, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE int64_t opencensus_proto_trace_v1_TraceConfig_max_number_of_links(const opencensus_proto_trace_v1_TraceConfig* msg) {
  int64_t default_val = (int64_t)0ll;
  int64_t ret;
  const upb_MiniTableField field = {7, 40, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}

UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_probability_sampler(opencensus_proto_trace_v1_TraceConfig *msg, opencensus_proto_trace_v1_ProbabilitySampler* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 48), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&opencensus__proto__trace__v1__ProbabilitySampler_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct opencensus_proto_trace_v1_ProbabilitySampler* opencensus_proto_trace_v1_TraceConfig_mutable_probability_sampler(opencensus_proto_trace_v1_TraceConfig* msg, upb_Arena* arena) {
  struct opencensus_proto_trace_v1_ProbabilitySampler* sub = (struct opencensus_proto_trace_v1_ProbabilitySampler*)opencensus_proto_trace_v1_TraceConfig_probability_sampler(msg);
  if (sub == NULL) {
    sub = (struct opencensus_proto_trace_v1_ProbabilitySampler*)_upb_Message_New(&opencensus__proto__trace__v1__ProbabilitySampler_msg_init, arena);
    if (sub) opencensus_proto_trace_v1_TraceConfig_set_probability_sampler(msg, sub);
  }
  return sub;
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_constant_sampler(opencensus_proto_trace_v1_TraceConfig *msg, opencensus_proto_trace_v1_ConstantSampler* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 48), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&opencensus__proto__trace__v1__ConstantSampler_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct opencensus_proto_trace_v1_ConstantSampler* opencensus_proto_trace_v1_TraceConfig_mutable_constant_sampler(opencensus_proto_trace_v1_TraceConfig* msg, upb_Arena* arena) {
  struct opencensus_proto_trace_v1_ConstantSampler* sub = (struct opencensus_proto_trace_v1_ConstantSampler*)opencensus_proto_trace_v1_TraceConfig_constant_sampler(msg);
  if (sub == NULL) {
    sub = (struct opencensus_proto_trace_v1_ConstantSampler*)_upb_Message_New(&opencensus__proto__trace__v1__ConstantSampler_msg_init, arena);
    if (sub) opencensus_proto_trace_v1_TraceConfig_set_constant_sampler(msg, sub);
  }
  return sub;
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_rate_limiting_sampler(opencensus_proto_trace_v1_TraceConfig *msg, opencensus_proto_trace_v1_RateLimitingSampler* value) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 48), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&opencensus__proto__trace__v1__RateLimitingSampler_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct opencensus_proto_trace_v1_RateLimitingSampler* opencensus_proto_trace_v1_TraceConfig_mutable_rate_limiting_sampler(opencensus_proto_trace_v1_TraceConfig* msg, upb_Arena* arena) {
  struct opencensus_proto_trace_v1_RateLimitingSampler* sub = (struct opencensus_proto_trace_v1_RateLimitingSampler*)opencensus_proto_trace_v1_TraceConfig_rate_limiting_sampler(msg);
  if (sub == NULL) {
    sub = (struct opencensus_proto_trace_v1_RateLimitingSampler*)_upb_Message_New(&opencensus__proto__trace__v1__RateLimitingSampler_msg_init, arena);
    if (sub) opencensus_proto_trace_v1_TraceConfig_set_rate_limiting_sampler(msg, sub);
  }
  return sub;
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_max_number_of_attributes(opencensus_proto_trace_v1_TraceConfig *msg, int64_t value) {
  const upb_MiniTableField field = {4, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_max_number_of_annotations(opencensus_proto_trace_v1_TraceConfig *msg, int64_t value) {
  const upb_MiniTableField field = {5, 24, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_max_number_of_message_events(opencensus_proto_trace_v1_TraceConfig *msg, int64_t value) {
  const upb_MiniTableField field = {6, 32, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE void opencensus_proto_trace_v1_TraceConfig_set_max_number_of_links(opencensus_proto_trace_v1_TraceConfig *msg, int64_t value) {
  const upb_MiniTableField field = {7, 40, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}

/* opencensus.proto.trace.v1.ProbabilitySampler */

UPB_INLINE opencensus_proto_trace_v1_ProbabilitySampler* opencensus_proto_trace_v1_ProbabilitySampler_new(upb_Arena* arena) {
  return (opencensus_proto_trace_v1_ProbabilitySampler*)_upb_Message_New(&opencensus__proto__trace__v1__ProbabilitySampler_msg_init, arena);
}
UPB_INLINE opencensus_proto_trace_v1_ProbabilitySampler* opencensus_proto_trace_v1_ProbabilitySampler_parse(const char* buf, size_t size, upb_Arena* arena) {
  opencensus_proto_trace_v1_ProbabilitySampler* ret = opencensus_proto_trace_v1_ProbabilitySampler_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__ProbabilitySampler_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE opencensus_proto_trace_v1_ProbabilitySampler* opencensus_proto_trace_v1_ProbabilitySampler_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  opencensus_proto_trace_v1_ProbabilitySampler* ret = opencensus_proto_trace_v1_ProbabilitySampler_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__ProbabilitySampler_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* opencensus_proto_trace_v1_ProbabilitySampler_serialize(const opencensus_proto_trace_v1_ProbabilitySampler* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__ProbabilitySampler_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* opencensus_proto_trace_v1_ProbabilitySampler_serialize_ex(const opencensus_proto_trace_v1_ProbabilitySampler* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__ProbabilitySampler_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void opencensus_proto_trace_v1_ProbabilitySampler_clear_samplingProbability(opencensus_proto_trace_v1_ProbabilitySampler* msg) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE double opencensus_proto_trace_v1_ProbabilitySampler_samplingProbability(const opencensus_proto_trace_v1_ProbabilitySampler* msg) {
  double default_val = 0;
  double ret;
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}

UPB_INLINE void opencensus_proto_trace_v1_ProbabilitySampler_set_samplingProbability(opencensus_proto_trace_v1_ProbabilitySampler *msg, double value) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}

/* opencensus.proto.trace.v1.ConstantSampler */

UPB_INLINE opencensus_proto_trace_v1_ConstantSampler* opencensus_proto_trace_v1_ConstantSampler_new(upb_Arena* arena) {
  return (opencensus_proto_trace_v1_ConstantSampler*)_upb_Message_New(&opencensus__proto__trace__v1__ConstantSampler_msg_init, arena);
}
UPB_INLINE opencensus_proto_trace_v1_ConstantSampler* opencensus_proto_trace_v1_ConstantSampler_parse(const char* buf, size_t size, upb_Arena* arena) {
  opencensus_proto_trace_v1_ConstantSampler* ret = opencensus_proto_trace_v1_ConstantSampler_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__ConstantSampler_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE opencensus_proto_trace_v1_ConstantSampler* opencensus_proto_trace_v1_ConstantSampler_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  opencensus_proto_trace_v1_ConstantSampler* ret = opencensus_proto_trace_v1_ConstantSampler_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__ConstantSampler_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* opencensus_proto_trace_v1_ConstantSampler_serialize(const opencensus_proto_trace_v1_ConstantSampler* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__ConstantSampler_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* opencensus_proto_trace_v1_ConstantSampler_serialize_ex(const opencensus_proto_trace_v1_ConstantSampler* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__ConstantSampler_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void opencensus_proto_trace_v1_ConstantSampler_clear_decision(opencensus_proto_trace_v1_ConstantSampler* msg) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE int32_t opencensus_proto_trace_v1_ConstantSampler_decision(const opencensus_proto_trace_v1_ConstantSampler* msg) {
  int32_t default_val = 0;
  int32_t ret;
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}

UPB_INLINE void opencensus_proto_trace_v1_ConstantSampler_set_decision(opencensus_proto_trace_v1_ConstantSampler *msg, int32_t value) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}

/* opencensus.proto.trace.v1.RateLimitingSampler */

UPB_INLINE opencensus_proto_trace_v1_RateLimitingSampler* opencensus_proto_trace_v1_RateLimitingSampler_new(upb_Arena* arena) {
  return (opencensus_proto_trace_v1_RateLimitingSampler*)_upb_Message_New(&opencensus__proto__trace__v1__RateLimitingSampler_msg_init, arena);
}
UPB_INLINE opencensus_proto_trace_v1_RateLimitingSampler* opencensus_proto_trace_v1_RateLimitingSampler_parse(const char* buf, size_t size, upb_Arena* arena) {
  opencensus_proto_trace_v1_RateLimitingSampler* ret = opencensus_proto_trace_v1_RateLimitingSampler_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__RateLimitingSampler_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE opencensus_proto_trace_v1_RateLimitingSampler* opencensus_proto_trace_v1_RateLimitingSampler_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  opencensus_proto_trace_v1_RateLimitingSampler* ret = opencensus_proto_trace_v1_RateLimitingSampler_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &opencensus__proto__trace__v1__RateLimitingSampler_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* opencensus_proto_trace_v1_RateLimitingSampler_serialize(const opencensus_proto_trace_v1_RateLimitingSampler* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__RateLimitingSampler_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* opencensus_proto_trace_v1_RateLimitingSampler_serialize_ex(const opencensus_proto_trace_v1_RateLimitingSampler* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &opencensus__proto__trace__v1__RateLimitingSampler_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void opencensus_proto_trace_v1_RateLimitingSampler_clear_qps(opencensus_proto_trace_v1_RateLimitingSampler* msg) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE int64_t opencensus_proto_trace_v1_RateLimitingSampler_qps(const opencensus_proto_trace_v1_RateLimitingSampler* msg) {
  int64_t default_val = (int64_t)0ll;
  int64_t ret;
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}

UPB_INLINE void opencensus_proto_trace_v1_RateLimitingSampler_set_qps(opencensus_proto_trace_v1_RateLimitingSampler *msg, int64_t value) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* OPENCENSUS_PROTO_TRACE_V1_TRACE_CONFIG_PROTO_UPB_H_ */
