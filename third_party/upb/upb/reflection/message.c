// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "upb/reflection/message.h"

#include <string.h>

#include "upb/hash/common.h"
#include "upb/message/accessors.h"
#include "upb/message/map.h"
#include "upb/message/message.h"
#include "upb/mini_table/field.h"
#include "upb/reflection/def.h"
#include "upb/reflection/def_pool.h"
#include "upb/reflection/def_type.h"
#include "upb/reflection/internal/field_def.h"
#include "upb/reflection/message_def.h"
#include "upb/reflection/oneof_def.h"

// Must be last.
#include "upb/port/def.inc"

bool upb_Message_HasFieldByDef(const upb_Message* msg, const upb_FieldDef* f) {
  UPB_ASSERT(upb_FieldDef_HasPresence(f));
  return upb_Message_HasField(msg, upb_FieldDef_MiniTable(f));
}

const upb_FieldDef* upb_Message_WhichOneof(const upb_Message* msg,
                                           const upb_OneofDef* o) {
  const upb_FieldDef* f = upb_OneofDef_Field(o, 0);
  if (upb_OneofDef_IsSynthetic(o)) {
    UPB_ASSERT(upb_OneofDef_FieldCount(o) == 1);
    return upb_Message_HasFieldByDef(msg, f) ? f : NULL;
  } else {
    const upb_MiniTableField* field = upb_FieldDef_MiniTable(f);
    uint32_t oneof_case = upb_Message_WhichOneofFieldNumber(msg, field);
    f = oneof_case ? upb_OneofDef_LookupNumber(o, oneof_case) : NULL;
    UPB_ASSERT((f != NULL) == (oneof_case != 0));
    return f;
  }
}

upb_MessageValue upb_Message_GetFieldByDef(const upb_Message* msg,
                                           const upb_FieldDef* f) {
  upb_MessageValue default_val = upb_FieldDef_Default(f);
  upb_MessageValue ret;
  _upb_Message_GetField(msg, upb_FieldDef_MiniTable(f), &default_val, &ret);
  return ret;
}

upb_MutableMessageValue upb_Message_Mutable(upb_Message* msg,
                                            const upb_FieldDef* f,
                                            upb_Arena* a) {
  UPB_ASSERT(upb_FieldDef_IsSubMessage(f) || upb_FieldDef_IsRepeated(f));
  if (upb_FieldDef_HasPresence(f) && !upb_Message_HasFieldByDef(msg, f)) {
    // We need to skip the upb_Message_GetFieldByDef() call in this case.
    goto make;
  }

  upb_MessageValue val = upb_Message_GetFieldByDef(msg, f);
  if (val.array_val) {
    return (upb_MutableMessageValue){.array = (upb_Array*)val.array_val};
  }

  upb_MutableMessageValue ret;
make:
  if (!a) return (upb_MutableMessageValue){.array = NULL};
  if (upb_FieldDef_IsMap(f)) {
    const upb_MessageDef* entry = upb_FieldDef_MessageSubDef(f);
    const upb_FieldDef* key =
        upb_MessageDef_FindFieldByNumber(entry, kUpb_MapEntry_KeyFieldNumber);
    const upb_FieldDef* value =
        upb_MessageDef_FindFieldByNumber(entry, kUpb_MapEntry_ValueFieldNumber);
    ret.map =
        upb_Map_New(a, upb_FieldDef_CType(key), upb_FieldDef_CType(value));
  } else if (upb_FieldDef_IsRepeated(f)) {
    ret.array = upb_Array_New(a, upb_FieldDef_CType(f));
  } else {
    UPB_ASSERT(upb_FieldDef_IsSubMessage(f));
    const upb_MessageDef* m = upb_FieldDef_MessageSubDef(f);
    ret.msg = upb_Message_New(upb_MessageDef_MiniTable(m), a);
  }

  val.array_val = ret.array;
  upb_Message_SetFieldByDef(msg, f, val, a);

  return ret;
}

bool upb_Message_SetFieldByDef(upb_Message* msg, const upb_FieldDef* f,
                               upb_MessageValue val, upb_Arena* a) {
  return _upb_Message_SetField(msg, upb_FieldDef_MiniTable(f), &val, a);
}

void upb_Message_ClearFieldByDef(upb_Message* msg, const upb_FieldDef* f) {
  upb_Message_ClearField(msg, upb_FieldDef_MiniTable(f));
}

void upb_Message_ClearByDef(upb_Message* msg, const upb_MessageDef* m) {
  upb_Message_Clear(msg, upb_MessageDef_MiniTable(m));
}

bool upb_Message_Next(const upb_Message* msg, const upb_MessageDef* m,
                      const upb_DefPool* ext_pool, const upb_FieldDef** out_f,
                      upb_MessageValue* out_val, size_t* iter) {
  size_t i = *iter;
  size_t n = upb_MessageDef_FieldCount(m);
  UPB_UNUSED(ext_pool);

  // Iterate over normal fields, returning the first one that is set.
  while (++i < n) {
    const upb_FieldDef* f = upb_MessageDef_Field(m, i);
    const upb_MiniTableField* field = upb_FieldDef_MiniTable(f);
    upb_MessageValue val = upb_Message_GetFieldByDef(msg, f);

    // Skip field if unset or empty.
    if (upb_MiniTableField_HasPresence(field)) {
      if (!upb_Message_HasFieldByDef(msg, f)) continue;
    } else {
      switch (upb_FieldMode_Get(field)) {
        case kUpb_FieldMode_Map:
          if (!val.map_val || upb_Map_Size(val.map_val) == 0) continue;
          break;
        case kUpb_FieldMode_Array:
          if (!val.array_val || upb_Array_Size(val.array_val) == 0) continue;
          break;
        case kUpb_FieldMode_Scalar:
          if (!_upb_MiniTable_ValueIsNonZero(&val, field)) continue;
          break;
      }
    }

    *out_val = val;
    *out_f = f;
    *iter = i;
    return true;
  }

  if (ext_pool) {
    // Return any extensions that are set.
    size_t count;
    const upb_Message_Extension* ext = _upb_Message_Getexts(msg, &count);
    if (i - n < count) {
      ext += count - 1 - (i - n);
      memcpy(out_val, &ext->data, sizeof(*out_val));
      *out_f = upb_DefPool_FindExtensionByMiniTable(ext_pool, ext->ext);
      *iter = i;
      return true;
    }
  }

  *iter = i;
  return false;
}

bool _upb_Message_DiscardUnknown(upb_Message* msg, const upb_MessageDef* m,
                                 int depth) {
  size_t iter = kUpb_Message_Begin;
  const upb_FieldDef* f;
  upb_MessageValue val;
  bool ret = true;

  if (--depth == 0) return false;

  _upb_Message_DiscardUnknown_shallow(msg);

  while (upb_Message_Next(msg, m, NULL /*ext_pool*/, &f, &val, &iter)) {
    const upb_MessageDef* subm = upb_FieldDef_MessageSubDef(f);
    if (!subm) continue;
    if (upb_FieldDef_IsMap(f)) {
      const upb_FieldDef* val_f = upb_MessageDef_FindFieldByNumber(subm, 2);
      const upb_MessageDef* val_m = upb_FieldDef_MessageSubDef(val_f);
      upb_Map* map = (upb_Map*)val.map_val;
      size_t iter = kUpb_Map_Begin;

      if (!val_m) continue;

      upb_MessageValue map_key, map_val;
      while (upb_Map_Next(map, &map_key, &map_val, &iter)) {
        if (!_upb_Message_DiscardUnknown((upb_Message*)map_val.msg_val, val_m,
                                         depth)) {
          ret = false;
        }
      }
    } else if (upb_FieldDef_IsRepeated(f)) {
      const upb_Array* arr = val.array_val;
      size_t i, n = upb_Array_Size(arr);
      for (i = 0; i < n; i++) {
        upb_MessageValue elem = upb_Array_Get(arr, i);
        if (!_upb_Message_DiscardUnknown((upb_Message*)elem.msg_val, subm,
                                         depth)) {
          ret = false;
        }
      }
    } else {
      if (!_upb_Message_DiscardUnknown((upb_Message*)val.msg_val, subm,
                                       depth)) {
        ret = false;
      }
    }
  }

  return ret;
}

bool upb_Message_DiscardUnknown(upb_Message* msg, const upb_MessageDef* m,
                                int maxdepth) {
  return _upb_Message_DiscardUnknown(msg, m, maxdepth);
}
