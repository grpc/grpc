/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/protobuf/compiler/plugin.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_PROTOBUF_COMPILER_PLUGIN_PROTO_UPB_H_
#define GOOGLE_PROTOBUF_COMPILER_PLUGIN_PROTO_UPB_H_

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

typedef struct google_protobuf_compiler_Version google_protobuf_compiler_Version;
typedef struct google_protobuf_compiler_CodeGeneratorRequest google_protobuf_compiler_CodeGeneratorRequest;
typedef struct google_protobuf_compiler_CodeGeneratorResponse google_protobuf_compiler_CodeGeneratorResponse;
typedef struct google_protobuf_compiler_CodeGeneratorResponse_File google_protobuf_compiler_CodeGeneratorResponse_File;
extern const upb_MiniTable* google_protobuf_compiler_Version_msg_init();
extern const upb_MiniTable* google_protobuf_compiler_CodeGeneratorRequest_msg_init();
extern const upb_MiniTable* google_protobuf_compiler_CodeGeneratorResponse_msg_init();
extern const upb_MiniTable* google_protobuf_compiler_CodeGeneratorResponse_File_msg_init();
struct google_protobuf_FileDescriptorProto;
struct google_protobuf_GeneratedCodeInfo;
extern const upb_MiniTable* google_protobuf_FileDescriptorProto_msg_init();
extern const upb_MiniTable* google_protobuf_GeneratedCodeInfo_msg_init();

typedef enum {
  google_protobuf_compiler_CodeGeneratorResponse_FEATURE_NONE = 0,
  google_protobuf_compiler_CodeGeneratorResponse_FEATURE_PROTO3_OPTIONAL = 1
} google_protobuf_compiler_CodeGeneratorResponse_Feature;


extern const upb_MiniTableEnum* google_protobuf_compiler_CodeGeneratorResponse_Feature_enum_init();

/* google.protobuf.compiler.Version */

UPB_INLINE google_protobuf_compiler_Version* google_protobuf_compiler_Version_new(upb_Arena* arena) {
  return (google_protobuf_compiler_Version*)_upb_Message_New(google_protobuf_compiler_Version_msg_init(), arena);
}
UPB_INLINE google_protobuf_compiler_Version* google_protobuf_compiler_Version_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_compiler_Version* ret = google_protobuf_compiler_Version_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_Version_msg_init(), NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_compiler_Version* google_protobuf_compiler_Version_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_compiler_Version* ret = google_protobuf_compiler_Version_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_Version_msg_init(), extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_compiler_Version_serialize(const google_protobuf_compiler_Version* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_Version_msg_init(), 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_compiler_Version_serialize_ex(const google_protobuf_compiler_Version* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_Version_msg_init(), options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_compiler_Version_clear_major(google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 1);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t google_protobuf_compiler_Version_major(const google_protobuf_compiler_Version* msg) {
  int32_t default_val = (int32_t)0;
  int32_t ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 1);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_Version_has_major(const google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 1);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_Version_clear_minor(google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 2);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t google_protobuf_compiler_Version_minor(const google_protobuf_compiler_Version* msg) {
  int32_t default_val = (int32_t)0;
  int32_t ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 2);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_Version_has_minor(const google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 2);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_Version_clear_patch(google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 3);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t google_protobuf_compiler_Version_patch(const google_protobuf_compiler_Version* msg) {
  int32_t default_val = (int32_t)0;
  int32_t ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 3);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_Version_has_patch(const google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 3);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_Version_clear_suffix(google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 4);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_compiler_Version_suffix(const google_protobuf_compiler_Version* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 4);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_Version_has_suffix(const google_protobuf_compiler_Version* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 4);
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void google_protobuf_compiler_Version_set_major(google_protobuf_compiler_Version *msg, int32_t value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 1);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_Version_set_minor(google_protobuf_compiler_Version *msg, int32_t value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 2);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_Version_set_patch(google_protobuf_compiler_Version *msg, int32_t value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 3);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_Version_set_suffix(google_protobuf_compiler_Version *msg, upb_StringView value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_Version_msg_init(), 4);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

/* google.protobuf.compiler.CodeGeneratorRequest */

UPB_INLINE google_protobuf_compiler_CodeGeneratorRequest* google_protobuf_compiler_CodeGeneratorRequest_new(upb_Arena* arena) {
  return (google_protobuf_compiler_CodeGeneratorRequest*)_upb_Message_New(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), arena);
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorRequest* google_protobuf_compiler_CodeGeneratorRequest_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_compiler_CodeGeneratorRequest* ret = google_protobuf_compiler_CodeGeneratorRequest_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_CodeGeneratorRequest_msg_init(), NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorRequest* google_protobuf_compiler_CodeGeneratorRequest_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_compiler_CodeGeneratorRequest* ret = google_protobuf_compiler_CodeGeneratorRequest_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_CodeGeneratorRequest_msg_init(), extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_compiler_CodeGeneratorRequest_serialize(const google_protobuf_compiler_CodeGeneratorRequest* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_compiler_CodeGeneratorRequest_serialize_ex(const google_protobuf_compiler_CodeGeneratorRequest* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_CodeGeneratorRequest_msg_init(), options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorRequest_clear_file_to_generate(google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 1);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView const* google_protobuf_compiler_CodeGeneratorRequest_file_to_generate(const google_protobuf_compiler_CodeGeneratorRequest* msg, size_t* size) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 1);
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorRequest_has_file_to_generate(const google_protobuf_compiler_CodeGeneratorRequest* msg) {
  size_t size;
  google_protobuf_compiler_CodeGeneratorRequest_file_to_generate(msg, &size);
  return size != 0;
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorRequest_clear_parameter(google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 2);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_compiler_CodeGeneratorRequest_parameter(const google_protobuf_compiler_CodeGeneratorRequest* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 2);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorRequest_has_parameter(const google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 2);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorRequest_clear_compiler_version(google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 3);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const google_protobuf_compiler_Version* google_protobuf_compiler_CodeGeneratorRequest_compiler_version(const google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const google_protobuf_compiler_Version* default_val = NULL;
  const google_protobuf_compiler_Version* ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 3);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorRequest_has_compiler_version(const google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 3);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorRequest_clear_proto_file(google_protobuf_compiler_CodeGeneratorRequest* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 15);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_FileDescriptorProto* const* google_protobuf_compiler_CodeGeneratorRequest_proto_file(const google_protobuf_compiler_CodeGeneratorRequest* msg, size_t* size) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 15);
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const struct google_protobuf_FileDescriptorProto* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorRequest_has_proto_file(const google_protobuf_compiler_CodeGeneratorRequest* msg) {
  size_t size;
  google_protobuf_compiler_CodeGeneratorRequest_proto_file(msg, &size);
  return size != 0;
}

UPB_INLINE upb_StringView* google_protobuf_compiler_CodeGeneratorRequest_mutable_file_to_generate(google_protobuf_compiler_CodeGeneratorRequest* msg, size_t* size) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 1);
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView*)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE upb_StringView* google_protobuf_compiler_CodeGeneratorRequest_resize_file_to_generate(google_protobuf_compiler_CodeGeneratorRequest* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 1);
  return (upb_StringView*)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorRequest_add_file_to_generate(google_protobuf_compiler_CodeGeneratorRequest* msg, upb_StringView val, upb_Arena* arena) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 1);
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return false;
  }
  _upb_Array_Set(arr, arr->size - 1, &val, sizeof(val));
  return true;
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorRequest_set_parameter(google_protobuf_compiler_CodeGeneratorRequest *msg, upb_StringView value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 2);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorRequest_set_compiler_version(google_protobuf_compiler_CodeGeneratorRequest *msg, google_protobuf_compiler_Version* value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 3);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_compiler_Version* google_protobuf_compiler_CodeGeneratorRequest_mutable_compiler_version(google_protobuf_compiler_CodeGeneratorRequest* msg, upb_Arena* arena) {
  struct google_protobuf_compiler_Version* sub = (struct google_protobuf_compiler_Version*)google_protobuf_compiler_CodeGeneratorRequest_compiler_version(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_compiler_Version*)_upb_Message_New(google_protobuf_compiler_Version_msg_init(), arena);
    if (sub) google_protobuf_compiler_CodeGeneratorRequest_set_compiler_version(msg, sub);
  }
  return sub;
}
UPB_INLINE struct google_protobuf_FileDescriptorProto** google_protobuf_compiler_CodeGeneratorRequest_mutable_proto_file(google_protobuf_compiler_CodeGeneratorRequest* msg, size_t* size) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 15);
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (struct google_protobuf_FileDescriptorProto**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE struct google_protobuf_FileDescriptorProto** google_protobuf_compiler_CodeGeneratorRequest_resize_proto_file(google_protobuf_compiler_CodeGeneratorRequest* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 15);
  return (struct google_protobuf_FileDescriptorProto**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct google_protobuf_FileDescriptorProto* google_protobuf_compiler_CodeGeneratorRequest_add_proto_file(google_protobuf_compiler_CodeGeneratorRequest* msg, upb_Arena* arena) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorRequest_msg_init(), 15);
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct google_protobuf_FileDescriptorProto* sub = (struct google_protobuf_FileDescriptorProto*)_upb_Message_New(google_protobuf_FileDescriptorProto_msg_init(), arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}

/* google.protobuf.compiler.CodeGeneratorResponse */

UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse* google_protobuf_compiler_CodeGeneratorResponse_new(upb_Arena* arena) {
  return (google_protobuf_compiler_CodeGeneratorResponse*)_upb_Message_New(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), arena);
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse* google_protobuf_compiler_CodeGeneratorResponse_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_compiler_CodeGeneratorResponse* ret = google_protobuf_compiler_CodeGeneratorResponse_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_CodeGeneratorResponse_msg_init(), NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse* google_protobuf_compiler_CodeGeneratorResponse_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_compiler_CodeGeneratorResponse* ret = google_protobuf_compiler_CodeGeneratorResponse_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_CodeGeneratorResponse_msg_init(), extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_compiler_CodeGeneratorResponse_serialize(const google_protobuf_compiler_CodeGeneratorResponse* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_compiler_CodeGeneratorResponse_serialize_ex(const google_protobuf_compiler_CodeGeneratorResponse* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_CodeGeneratorResponse_msg_init(), options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_clear_error(google_protobuf_compiler_CodeGeneratorResponse* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 1);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_compiler_CodeGeneratorResponse_error(const google_protobuf_compiler_CodeGeneratorResponse* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 1);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_has_error(const google_protobuf_compiler_CodeGeneratorResponse* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 1);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_clear_supported_features(google_protobuf_compiler_CodeGeneratorResponse* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 2);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE uint64_t google_protobuf_compiler_CodeGeneratorResponse_supported_features(const google_protobuf_compiler_CodeGeneratorResponse* msg) {
  uint64_t default_val = (uint64_t)0ull;
  uint64_t ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 2);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_has_supported_features(const google_protobuf_compiler_CodeGeneratorResponse* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 2);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_clear_file(google_protobuf_compiler_CodeGeneratorResponse* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 15);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const google_protobuf_compiler_CodeGeneratorResponse_File* const* google_protobuf_compiler_CodeGeneratorResponse_file(const google_protobuf_compiler_CodeGeneratorResponse* msg, size_t* size) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 15);
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const google_protobuf_compiler_CodeGeneratorResponse_File* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_has_file(const google_protobuf_compiler_CodeGeneratorResponse* msg) {
  size_t size;
  google_protobuf_compiler_CodeGeneratorResponse_file(msg, &size);
  return size != 0;
}

UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_set_error(google_protobuf_compiler_CodeGeneratorResponse *msg, upb_StringView value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 1);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_set_supported_features(google_protobuf_compiler_CodeGeneratorResponse *msg, uint64_t value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 2);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse_File** google_protobuf_compiler_CodeGeneratorResponse_mutable_file(google_protobuf_compiler_CodeGeneratorResponse* msg, size_t* size) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 15);
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (google_protobuf_compiler_CodeGeneratorResponse_File**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse_File** google_protobuf_compiler_CodeGeneratorResponse_resize_file(google_protobuf_compiler_CodeGeneratorResponse* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 15);
  return (google_protobuf_compiler_CodeGeneratorResponse_File**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct google_protobuf_compiler_CodeGeneratorResponse_File* google_protobuf_compiler_CodeGeneratorResponse_add_file(google_protobuf_compiler_CodeGeneratorResponse* msg, upb_Arena* arena) {
  upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_msg_init(), 15);
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct google_protobuf_compiler_CodeGeneratorResponse_File* sub = (struct google_protobuf_compiler_CodeGeneratorResponse_File*)_upb_Message_New(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}

/* google.protobuf.compiler.CodeGeneratorResponse.File */

UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse_File* google_protobuf_compiler_CodeGeneratorResponse_File_new(upb_Arena* arena) {
  return (google_protobuf_compiler_CodeGeneratorResponse_File*)_upb_Message_New(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), arena);
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse_File* google_protobuf_compiler_CodeGeneratorResponse_File_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_compiler_CodeGeneratorResponse_File* ret = google_protobuf_compiler_CodeGeneratorResponse_File_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_compiler_CodeGeneratorResponse_File* google_protobuf_compiler_CodeGeneratorResponse_File_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_compiler_CodeGeneratorResponse_File* ret = google_protobuf_compiler_CodeGeneratorResponse_File_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_compiler_CodeGeneratorResponse_File_serialize(const google_protobuf_compiler_CodeGeneratorResponse_File* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_compiler_CodeGeneratorResponse_File_serialize_ex(const google_protobuf_compiler_CodeGeneratorResponse_File* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_clear_name(google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 1);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_compiler_CodeGeneratorResponse_File_name(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 1);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_File_has_name(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 1);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_clear_insertion_point(google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 2);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_compiler_CodeGeneratorResponse_File_insertion_point(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 2);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_File_has_insertion_point(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 2);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_clear_content(google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 15);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_compiler_CodeGeneratorResponse_File_content(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 15);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_File_has_content(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 15);
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_clear_generated_code_info(google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 16);
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_GeneratedCodeInfo* google_protobuf_compiler_CodeGeneratorResponse_File_generated_code_info(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const struct google_protobuf_GeneratedCodeInfo* default_val = NULL;
  const struct google_protobuf_GeneratedCodeInfo* ret;
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 16);
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_compiler_CodeGeneratorResponse_File_has_generated_code_info(const google_protobuf_compiler_CodeGeneratorResponse_File* msg) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 16);
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_set_name(google_protobuf_compiler_CodeGeneratorResponse_File *msg, upb_StringView value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 1);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_set_insertion_point(google_protobuf_compiler_CodeGeneratorResponse_File *msg, upb_StringView value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 2);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_set_content(google_protobuf_compiler_CodeGeneratorResponse_File *msg, upb_StringView value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 15);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_compiler_CodeGeneratorResponse_File_set_generated_code_info(google_protobuf_compiler_CodeGeneratorResponse_File *msg, struct google_protobuf_GeneratedCodeInfo* value) {
  const upb_MiniTableField field = *upb_MiniTable_FindFieldByNumber(google_protobuf_compiler_CodeGeneratorResponse_File_msg_init(), 16);
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_GeneratedCodeInfo* google_protobuf_compiler_CodeGeneratorResponse_File_mutable_generated_code_info(google_protobuf_compiler_CodeGeneratorResponse_File* msg, upb_Arena* arena) {
  struct google_protobuf_GeneratedCodeInfo* sub = (struct google_protobuf_GeneratedCodeInfo*)google_protobuf_compiler_CodeGeneratorResponse_File_generated_code_info(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_GeneratedCodeInfo*)_upb_Message_New(google_protobuf_GeneratedCodeInfo_msg_init(), arena);
    if (sub) google_protobuf_compiler_CodeGeneratorResponse_File_set_generated_code_info(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile google_protobuf_compiler_plugin_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_PROTOBUF_COMPILER_PLUGIN_PROTO_UPB_H_ */
