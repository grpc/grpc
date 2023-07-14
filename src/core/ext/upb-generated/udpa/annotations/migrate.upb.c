/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     udpa/annotations/migrate.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "udpa/annotations/migrate.upb.h"
#include "google/protobuf/descriptor.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField udpa_annotations_MigrateAnnotation__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable udpa_annotations_MigrateAnnotation_msg_init = {
  NULL,
  &udpa_annotations_MigrateAnnotation__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTableField udpa_annotations_FieldMigrateAnnotation__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable udpa_annotations_FieldMigrateAnnotation_msg_init = {
  NULL,
  &udpa_annotations_FieldMigrateAnnotation__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField udpa_annotations_FileMigrateAnnotation__fields[1] = {
  {2, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable udpa_annotations_FileMigrateAnnotation_msg_init = {
  NULL,
  &udpa_annotations_FileMigrateAnnotation__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[3] = {
  &udpa_annotations_MigrateAnnotation_msg_init,
  &udpa_annotations_FieldMigrateAnnotation_msg_init,
  &udpa_annotations_FileMigrateAnnotation_msg_init,
};

extern const upb_MiniTable google_protobuf_EnumOptions_msg_init;
extern const upb_MiniTable google_protobuf_EnumValueOptions_msg_init;
extern const upb_MiniTable google_protobuf_FieldOptions_msg_init;
extern const upb_MiniTable google_protobuf_FileOptions_msg_init;
extern const upb_MiniTable google_protobuf_MessageOptions_msg_init;
extern const upb_MiniTable udpa_annotations_FieldMigrateAnnotation_msg_init;
extern const upb_MiniTable udpa_annotations_FileMigrateAnnotation_msg_init;
extern const upb_MiniTable udpa_annotations_MigrateAnnotation_msg_init;
const upb_MiniTableExtension udpa_annotations_message_migrate_ext = {
  {171962766, 0, 0, 0, 11, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsExtension | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  &google_protobuf_MessageOptions_msg_init,
  {.submsg = &udpa_annotations_MigrateAnnotation_msg_init},

};
const upb_MiniTableExtension udpa_annotations_field_migrate_ext = {
  {171962766, 0, 0, 0, 11, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsExtension | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  &google_protobuf_FieldOptions_msg_init,
  {.submsg = &udpa_annotations_FieldMigrateAnnotation_msg_init},

};
const upb_MiniTableExtension udpa_annotations_enum_migrate_ext = {
  {171962766, 0, 0, 0, 11, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsExtension | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  &google_protobuf_EnumOptions_msg_init,
  {.submsg = &udpa_annotations_MigrateAnnotation_msg_init},

};
const upb_MiniTableExtension udpa_annotations_enum_value_migrate_ext = {
  {171962766, 0, 0, 0, 11, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsExtension | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  &google_protobuf_EnumValueOptions_msg_init,
  {.submsg = &udpa_annotations_MigrateAnnotation_msg_init},

};
const upb_MiniTableExtension udpa_annotations_file_migrate_ext = {
  {171962766, 0, 0, 0, 11, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsExtension | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  &google_protobuf_FileOptions_msg_init,
  {.submsg = &udpa_annotations_FileMigrateAnnotation_msg_init},

};

static const upb_MiniTableExtension *extensions_layout[5] = {
  &udpa_annotations_message_migrate_ext,
  &udpa_annotations_field_migrate_ext,
  &udpa_annotations_enum_migrate_ext,
  &udpa_annotations_enum_value_migrate_ext,
  &udpa_annotations_file_migrate_ext,
};

const upb_MiniTableFile udpa_annotations_migrate_proto_upb_file_layout = {
  messages_layout,
  NULL,
  extensions_layout,
  3,
  0,
  5,
};

#include "upb/port/undef.inc"

