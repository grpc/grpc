/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/substitution_format_string.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_SUBSTITUTION_FORMAT_STRING_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_SUBSTITUTION_FORMAT_STRING_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_core_v3_SubstitutionFormatString;
typedef struct envoy_config_core_v3_SubstitutionFormatString envoy_config_core_v3_SubstitutionFormatString;
extern const upb_MiniTable envoy_config_core_v3_SubstitutionFormatString_msginit;
struct envoy_config_core_v3_DataSource;
struct envoy_config_core_v3_TypedExtensionConfig;
struct google_protobuf_Struct;
extern const upb_MiniTable envoy_config_core_v3_DataSource_msginit;
extern const upb_MiniTable envoy_config_core_v3_TypedExtensionConfig_msginit;
extern const upb_MiniTable google_protobuf_Struct_msginit;



/* envoy.config.core.v3.SubstitutionFormatString */

UPB_INLINE envoy_config_core_v3_SubstitutionFormatString* envoy_config_core_v3_SubstitutionFormatString_new(upb_Arena* arena) {
  return (envoy_config_core_v3_SubstitutionFormatString*)_upb_Message_New(&envoy_config_core_v3_SubstitutionFormatString_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_SubstitutionFormatString* envoy_config_core_v3_SubstitutionFormatString_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_SubstitutionFormatString* ret = envoy_config_core_v3_SubstitutionFormatString_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_SubstitutionFormatString_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_SubstitutionFormatString* envoy_config_core_v3_SubstitutionFormatString_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_SubstitutionFormatString* ret = envoy_config_core_v3_SubstitutionFormatString_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_SubstitutionFormatString_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_SubstitutionFormatString_serialize(const envoy_config_core_v3_SubstitutionFormatString* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_SubstitutionFormatString_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_SubstitutionFormatString_serialize_ex(const envoy_config_core_v3_SubstitutionFormatString* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_SubstitutionFormatString_msginit, options, arena, len);
}
typedef enum {
  envoy_config_core_v3_SubstitutionFormatString_format_text_format = 1,
  envoy_config_core_v3_SubstitutionFormatString_format_json_format = 2,
  envoy_config_core_v3_SubstitutionFormatString_format_text_format_source = 5,
  envoy_config_core_v3_SubstitutionFormatString_format_NOT_SET = 0
} envoy_config_core_v3_SubstitutionFormatString_format_oneofcases;
UPB_INLINE envoy_config_core_v3_SubstitutionFormatString_format_oneofcases envoy_config_core_v3_SubstitutionFormatString_format_case(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return (envoy_config_core_v3_SubstitutionFormatString_format_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t);
}
UPB_INLINE bool envoy_config_core_v3_SubstitutionFormatString_has_text_format(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 1;
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_clear_text_format(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(8, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(4, 4), envoy_config_core_v3_SubstitutionFormatString_format_NOT_SET);
}
UPB_INLINE upb_StringView envoy_config_core_v3_SubstitutionFormatString_text_format(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(8, 8), UPB_SIZE(4, 4), 1, upb_StringView_FromString(""));
}
UPB_INLINE bool envoy_config_core_v3_SubstitutionFormatString_has_json_format(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 2;
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_clear_json_format(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  UPB_WRITE_ONEOF(msg, struct google_protobuf_Struct*, UPB_SIZE(8, 8), 0, UPB_SIZE(4, 4), envoy_config_core_v3_SubstitutionFormatString_format_NOT_SET);
}
UPB_INLINE const struct google_protobuf_Struct* envoy_config_core_v3_SubstitutionFormatString_json_format(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return UPB_READ_ONEOF(msg, const struct google_protobuf_Struct*, UPB_SIZE(8, 8), UPB_SIZE(4, 4), 2, NULL);
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_clear_omit_empty_values(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = 0;
}
UPB_INLINE bool envoy_config_core_v3_SubstitutionFormatString_omit_empty_values(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool);
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_clear_content_type(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_config_core_v3_SubstitutionFormatString_content_type(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_StringView);
}
UPB_INLINE bool envoy_config_core_v3_SubstitutionFormatString_has_text_format_source(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 5;
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_clear_text_format_source(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  UPB_WRITE_ONEOF(msg, struct envoy_config_core_v3_DataSource*, UPB_SIZE(8, 8), 0, UPB_SIZE(4, 4), envoy_config_core_v3_SubstitutionFormatString_format_NOT_SET);
}
UPB_INLINE const struct envoy_config_core_v3_DataSource* envoy_config_core_v3_SubstitutionFormatString_text_format_source(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return UPB_READ_ONEOF(msg, const struct envoy_config_core_v3_DataSource*, UPB_SIZE(8, 8), UPB_SIZE(4, 4), 5, NULL);
}
UPB_INLINE bool envoy_config_core_v3_SubstitutionFormatString_has_formatters(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(24, 40));
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_clear_formatters(const envoy_config_core_v3_SubstitutionFormatString* msg) {
  _upb_array_detach(msg, UPB_SIZE(24, 40));
}
UPB_INLINE const struct envoy_config_core_v3_TypedExtensionConfig* const* envoy_config_core_v3_SubstitutionFormatString_formatters(const envoy_config_core_v3_SubstitutionFormatString* msg, size_t* len) {
  return (const struct envoy_config_core_v3_TypedExtensionConfig* const*)_upb_array_accessor(msg, UPB_SIZE(24, 40), len);
}

UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_set_text_format(envoy_config_core_v3_SubstitutionFormatString *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(8, 8), value, UPB_SIZE(4, 4), 1);
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_set_json_format(envoy_config_core_v3_SubstitutionFormatString *msg, struct google_protobuf_Struct* value) {
  UPB_WRITE_ONEOF(msg, struct google_protobuf_Struct*, UPB_SIZE(8, 8), value, UPB_SIZE(4, 4), 2);
}
UPB_INLINE struct google_protobuf_Struct* envoy_config_core_v3_SubstitutionFormatString_mutable_json_format(envoy_config_core_v3_SubstitutionFormatString* msg, upb_Arena* arena) {
  struct google_protobuf_Struct* sub = (struct google_protobuf_Struct*)envoy_config_core_v3_SubstitutionFormatString_json_format(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Struct*)_upb_Message_New(&google_protobuf_Struct_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_SubstitutionFormatString_set_json_format(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_set_omit_empty_values(envoy_config_core_v3_SubstitutionFormatString *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = value;
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_set_content_type(envoy_config_core_v3_SubstitutionFormatString *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_StringView) = value;
}
UPB_INLINE void envoy_config_core_v3_SubstitutionFormatString_set_text_format_source(envoy_config_core_v3_SubstitutionFormatString *msg, struct envoy_config_core_v3_DataSource* value) {
  UPB_WRITE_ONEOF(msg, struct envoy_config_core_v3_DataSource*, UPB_SIZE(8, 8), value, UPB_SIZE(4, 4), 5);
}
UPB_INLINE struct envoy_config_core_v3_DataSource* envoy_config_core_v3_SubstitutionFormatString_mutable_text_format_source(envoy_config_core_v3_SubstitutionFormatString* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_DataSource* sub = (struct envoy_config_core_v3_DataSource*)envoy_config_core_v3_SubstitutionFormatString_text_format_source(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_DataSource*)_upb_Message_New(&envoy_config_core_v3_DataSource_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_SubstitutionFormatString_set_text_format_source(msg, sub);
  }
  return sub;
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig** envoy_config_core_v3_SubstitutionFormatString_mutable_formatters(envoy_config_core_v3_SubstitutionFormatString* msg, size_t* len) {
  return (struct envoy_config_core_v3_TypedExtensionConfig**)_upb_array_mutable_accessor(msg, UPB_SIZE(24, 40), len);
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig** envoy_config_core_v3_SubstitutionFormatString_resize_formatters(envoy_config_core_v3_SubstitutionFormatString* msg, size_t len, upb_Arena* arena) {
  return (struct envoy_config_core_v3_TypedExtensionConfig**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(24, 40), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_config_core_v3_TypedExtensionConfig* envoy_config_core_v3_SubstitutionFormatString_add_formatters(envoy_config_core_v3_SubstitutionFormatString* msg, upb_Arena* arena) {
  struct envoy_config_core_v3_TypedExtensionConfig* sub = (struct envoy_config_core_v3_TypedExtensionConfig*)_upb_Message_New(&envoy_config_core_v3_TypedExtensionConfig_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(24, 40), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

extern const upb_MiniTable_File envoy_config_core_v3_substitution_format_string_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_SUBSTITUTION_FORMAT_STRING_PROTO_UPB_H_ */
