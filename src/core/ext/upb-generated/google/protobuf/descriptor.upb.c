/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/protobuf/descriptor.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "google/protobuf/descriptor.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub google_protobuf_FileDescriptorSet_submsgs[1] = {
  {.submsg = &google_protobuf_FileDescriptorProto_msginit},
};

static const upb_MiniTable_Field google_protobuf_FileDescriptorSet__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_FileDescriptorSet_msginit = {
  &google_protobuf_FileDescriptorSet_submsgs[0],
  &google_protobuf_FileDescriptorSet__fields[0],
  UPB_SIZE(8, 8), 1, upb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_FileDescriptorProto_submsgs[6] = {
  {.submsg = &google_protobuf_DescriptorProto_msginit},
  {.submsg = &google_protobuf_EnumDescriptorProto_msginit},
  {.submsg = &google_protobuf_FieldDescriptorProto_msginit},
  {.submsg = &google_protobuf_FileOptions_msginit},
  {.submsg = &google_protobuf_ServiceDescriptorProto_msginit},
  {.submsg = &google_protobuf_SourceCodeInfo_msginit},
};

static const upb_MiniTable_Field google_protobuf_FileDescriptorProto__fields[12] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 24), 2, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {3, UPB_SIZE(36, 72), 0, 0, 12, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {4, UPB_SIZE(40, 80), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {5, UPB_SIZE(44, 88), 0, 1, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {6, UPB_SIZE(48, 96), 0, 4, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {7, UPB_SIZE(52, 104), 0, 2, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {8, UPB_SIZE(28, 56), 3, 3, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {9, UPB_SIZE(32, 64), 4, 5, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {10, UPB_SIZE(56, 112), 0, 0, 5, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {11, UPB_SIZE(60, 120), 0, 0, 5, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {12, UPB_SIZE(20, 40), 5, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_FileDescriptorProto_msginit = {
  &google_protobuf_FileDescriptorProto_submsgs[0],
  &google_protobuf_FileDescriptorProto__fields[0],
  UPB_SIZE(64, 128), 12, upb_ExtMode_NonExtendable, 12, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_DescriptorProto_submsgs[7] = {
  {.submsg = &google_protobuf_DescriptorProto_msginit},
  {.submsg = &google_protobuf_DescriptorProto_ExtensionRange_msginit},
  {.submsg = &google_protobuf_DescriptorProto_ReservedRange_msginit},
  {.submsg = &google_protobuf_EnumDescriptorProto_msginit},
  {.submsg = &google_protobuf_FieldDescriptorProto_msginit},
  {.submsg = &google_protobuf_MessageOptions_msginit},
  {.submsg = &google_protobuf_OneofDescriptorProto_msginit},
};

static const upb_MiniTable_Field google_protobuf_DescriptorProto__fields[10] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 32), 0, 4, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 40), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 48), 0, 3, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {5, UPB_SIZE(28, 56), 0, 1, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {6, UPB_SIZE(32, 64), 0, 4, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {7, UPB_SIZE(12, 24), 2, 5, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {8, UPB_SIZE(36, 72), 0, 6, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {9, UPB_SIZE(40, 80), 0, 2, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {10, UPB_SIZE(44, 88), 0, 0, 12, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_DescriptorProto_msginit = {
  &google_protobuf_DescriptorProto_submsgs[0],
  &google_protobuf_DescriptorProto__fields[0],
  UPB_SIZE(48, 96), 10, upb_ExtMode_NonExtendable, 10, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_DescriptorProto_ExtensionRange_submsgs[1] = {
  {.submsg = &google_protobuf_ExtensionRangeOptions_msginit},
};

static const upb_MiniTable_Field google_protobuf_DescriptorProto_ExtensionRange__fields[3] = {
  {1, UPB_SIZE(4, 4), 1, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 8), 2, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), 3, 0, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_DescriptorProto_ExtensionRange_msginit = {
  &google_protobuf_DescriptorProto_ExtensionRange_submsgs[0],
  &google_protobuf_DescriptorProto_ExtensionRange__fields[0],
  UPB_SIZE(16, 24), 3, upb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable_Field google_protobuf_DescriptorProto_ReservedRange__fields[2] = {
  {1, UPB_SIZE(4, 4), 1, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 8), 2, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_DescriptorProto_ReservedRange_msginit = {
  NULL,
  &google_protobuf_DescriptorProto_ReservedRange__fields[0],
  UPB_SIZE(16, 16), 2, upb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_ExtensionRangeOptions_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
};

static const upb_MiniTable_Field google_protobuf_ExtensionRangeOptions__fields[1] = {
  {999, UPB_SIZE(0, 0), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_ExtensionRangeOptions_msginit = {
  &google_protobuf_ExtensionRangeOptions_submsgs[0],
  &google_protobuf_ExtensionRangeOptions__fields[0],
  UPB_SIZE(8, 8), 1, upb_ExtMode_Extendable, 0, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_FieldDescriptorProto_submsgs[3] = {
  {.submsg = &google_protobuf_FieldOptions_msginit},
  {.subenum = &google_protobuf_FieldDescriptorProto_Label_enuminit},
  {.subenum = &google_protobuf_FieldDescriptorProto_Type_enuminit},
};

static const upb_MiniTable_Field google_protobuf_FieldDescriptorProto__fields[11] = {
  {1, UPB_SIZE(24, 24), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(32, 40), 2, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 12), 3, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 4), 4, 1, 14, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {5, UPB_SIZE(8, 8), 5, 2, 14, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {6, UPB_SIZE(40, 56), 6, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {7, UPB_SIZE(48, 72), 7, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {8, UPB_SIZE(64, 104), 8, 0, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {9, UPB_SIZE(16, 16), 9, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {10, UPB_SIZE(56, 88), 10, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {17, UPB_SIZE(20, 20), 11, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_FieldDescriptorProto_msginit = {
  &google_protobuf_FieldDescriptorProto_submsgs[0],
  &google_protobuf_FieldDescriptorProto__fields[0],
  UPB_SIZE(72, 112), 11, upb_ExtMode_NonExtendable, 10, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_OneofDescriptorProto_submsgs[1] = {
  {.submsg = &google_protobuf_OneofOptions_msginit},
};

static const upb_MiniTable_Field google_protobuf_OneofDescriptorProto__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 24), 2, 0, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_OneofDescriptorProto_msginit = {
  &google_protobuf_OneofDescriptorProto_submsgs[0],
  &google_protobuf_OneofDescriptorProto__fields[0],
  UPB_SIZE(16, 32), 2, upb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_EnumDescriptorProto_submsgs[3] = {
  {.submsg = &google_protobuf_EnumDescriptorProto_EnumReservedRange_msginit},
  {.submsg = &google_protobuf_EnumOptions_msginit},
  {.submsg = &google_protobuf_EnumValueDescriptorProto_msginit},
};

static const upb_MiniTable_Field google_protobuf_EnumDescriptorProto__fields[5] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 32), 0, 2, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), 2, 1, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {4, UPB_SIZE(20, 40), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {5, UPB_SIZE(24, 48), 0, 0, 12, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_EnumDescriptorProto_msginit = {
  &google_protobuf_EnumDescriptorProto_submsgs[0],
  &google_protobuf_EnumDescriptorProto__fields[0],
  UPB_SIZE(32, 64), 5, upb_ExtMode_NonExtendable, 5, 255, 0,
};

static const upb_MiniTable_Field google_protobuf_EnumDescriptorProto_EnumReservedRange__fields[2] = {
  {1, UPB_SIZE(4, 4), 1, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 8), 2, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_EnumDescriptorProto_EnumReservedRange_msginit = {
  NULL,
  &google_protobuf_EnumDescriptorProto_EnumReservedRange__fields[0],
  UPB_SIZE(16, 16), 2, upb_ExtMode_NonExtendable, 2, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_EnumValueDescriptorProto_submsgs[1] = {
  {.submsg = &google_protobuf_EnumValueOptions_msginit},
};

static const upb_MiniTable_Field google_protobuf_EnumValueDescriptorProto__fields[3] = {
  {1, UPB_SIZE(8, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 4), 2, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 24), 3, 0, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_EnumValueDescriptorProto_msginit = {
  &google_protobuf_EnumValueDescriptorProto_submsgs[0],
  &google_protobuf_EnumValueDescriptorProto__fields[0],
  UPB_SIZE(24, 32), 3, upb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_ServiceDescriptorProto_submsgs[2] = {
  {.submsg = &google_protobuf_MethodDescriptorProto_msginit},
  {.submsg = &google_protobuf_ServiceOptions_msginit},
};

static const upb_MiniTable_Field google_protobuf_ServiceDescriptorProto__fields[3] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 32), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), 2, 1, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_ServiceDescriptorProto_msginit = {
  &google_protobuf_ServiceDescriptorProto_submsgs[0],
  &google_protobuf_ServiceDescriptorProto__fields[0],
  UPB_SIZE(24, 48), 3, upb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_MethodDescriptorProto_submsgs[1] = {
  {.submsg = &google_protobuf_MethodOptions_msginit},
};

static const upb_MiniTable_Field google_protobuf_MethodDescriptorProto__fields[6] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 24), 2, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 40), 3, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {4, UPB_SIZE(28, 56), 4, 0, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {5, UPB_SIZE(1, 1), 5, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {6, UPB_SIZE(2, 2), 6, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_MethodDescriptorProto_msginit = {
  &google_protobuf_MethodDescriptorProto_submsgs[0],
  &google_protobuf_MethodDescriptorProto__fields[0],
  UPB_SIZE(32, 64), 6, upb_ExtMode_NonExtendable, 6, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_FileOptions_submsgs[2] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
  {.subenum = &google_protobuf_FileOptions_OptimizeMode_enuminit},
};

static const upb_MiniTable_Field google_protobuf_FileOptions__fields[21] = {
  {1, UPB_SIZE(20, 24), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {8, UPB_SIZE(28, 40), 2, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {9, UPB_SIZE(4, 4), 3, 1, 14, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {10, UPB_SIZE(8, 8), 4, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {11, UPB_SIZE(36, 56), 5, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {16, UPB_SIZE(9, 9), 6, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {17, UPB_SIZE(10, 10), 7, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {18, UPB_SIZE(11, 11), 8, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {20, UPB_SIZE(12, 12), 9, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {23, UPB_SIZE(13, 13), 10, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {27, UPB_SIZE(14, 14), 11, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {31, UPB_SIZE(15, 15), 12, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {36, UPB_SIZE(44, 72), 13, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {37, UPB_SIZE(52, 88), 14, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {39, UPB_SIZE(60, 104), 15, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {40, UPB_SIZE(68, 120), 16, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {41, UPB_SIZE(76, 136), 17, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {42, UPB_SIZE(16, 16), 18, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {44, UPB_SIZE(84, 152), 19, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {45, UPB_SIZE(92, 168), 20, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {999, UPB_SIZE(100, 184), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_FileOptions_msginit = {
  &google_protobuf_FileOptions_submsgs[0],
  &google_protobuf_FileOptions__fields[0],
  UPB_SIZE(104, 192), 21, upb_ExtMode_Extendable, 1, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_MessageOptions_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
};

static const upb_MiniTable_Field google_protobuf_MessageOptions__fields[5] = {
  {1, UPB_SIZE(1, 1), 1, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {2, UPB_SIZE(2, 2), 2, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {3, UPB_SIZE(3, 3), 3, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {7, UPB_SIZE(4, 4), 4, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {999, UPB_SIZE(8, 8), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_MessageOptions_msginit = {
  &google_protobuf_MessageOptions_submsgs[0],
  &google_protobuf_MessageOptions__fields[0],
  UPB_SIZE(16, 16), 5, upb_ExtMode_Extendable, 3, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_FieldOptions_submsgs[3] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
  {.subenum = &google_protobuf_FieldOptions_CType_enuminit},
  {.subenum = &google_protobuf_FieldOptions_JSType_enuminit},
};

static const upb_MiniTable_Field google_protobuf_FieldOptions__fields[7] = {
  {1, UPB_SIZE(4, 4), 1, 1, 14, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 12), 2, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {3, UPB_SIZE(13, 13), 3, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {5, UPB_SIZE(14, 14), 4, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {6, UPB_SIZE(8, 8), 5, 2, 14, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {10, UPB_SIZE(15, 15), 6, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {999, UPB_SIZE(16, 16), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_FieldOptions_msginit = {
  &google_protobuf_FieldOptions_submsgs[0],
  &google_protobuf_FieldOptions__fields[0],
  UPB_SIZE(24, 24), 7, upb_ExtMode_Extendable, 3, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_OneofOptions_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
};

static const upb_MiniTable_Field google_protobuf_OneofOptions__fields[1] = {
  {999, UPB_SIZE(0, 0), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_OneofOptions_msginit = {
  &google_protobuf_OneofOptions_submsgs[0],
  &google_protobuf_OneofOptions__fields[0],
  UPB_SIZE(8, 8), 1, upb_ExtMode_Extendable, 0, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_EnumOptions_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
};

static const upb_MiniTable_Field google_protobuf_EnumOptions__fields[3] = {
  {2, UPB_SIZE(1, 1), 1, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {3, UPB_SIZE(2, 2), 2, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {999, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_EnumOptions_msginit = {
  &google_protobuf_EnumOptions_submsgs[0],
  &google_protobuf_EnumOptions__fields[0],
  UPB_SIZE(8, 16), 3, upb_ExtMode_Extendable, 0, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_EnumValueOptions_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
};

static const upb_MiniTable_Field google_protobuf_EnumValueOptions__fields[2] = {
  {1, UPB_SIZE(1, 1), 1, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {999, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_EnumValueOptions_msginit = {
  &google_protobuf_EnumValueOptions_submsgs[0],
  &google_protobuf_EnumValueOptions__fields[0],
  UPB_SIZE(8, 16), 2, upb_ExtMode_Extendable, 1, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_ServiceOptions_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
};

static const upb_MiniTable_Field google_protobuf_ServiceOptions__fields[2] = {
  {33, UPB_SIZE(1, 1), 1, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {999, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_ServiceOptions_msginit = {
  &google_protobuf_ServiceOptions_submsgs[0],
  &google_protobuf_ServiceOptions__fields[0],
  UPB_SIZE(8, 16), 2, upb_ExtMode_Extendable, 0, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_MethodOptions_submsgs[2] = {
  {.submsg = &google_protobuf_UninterpretedOption_msginit},
  {.subenum = &google_protobuf_MethodOptions_IdempotencyLevel_enuminit},
};

static const upb_MiniTable_Field google_protobuf_MethodOptions__fields[3] = {
  {33, UPB_SIZE(8, 8), 1, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
  {34, UPB_SIZE(4, 4), 2, 1, 14, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {999, UPB_SIZE(12, 16), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_MethodOptions_msginit = {
  &google_protobuf_MethodOptions_submsgs[0],
  &google_protobuf_MethodOptions__fields[0],
  UPB_SIZE(16, 24), 3, upb_ExtMode_Extendable, 0, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_UninterpretedOption_submsgs[1] = {
  {.submsg = &google_protobuf_UninterpretedOption_NamePart_msginit},
};

static const upb_MiniTable_Field google_protobuf_UninterpretedOption__fields[7] = {
  {2, UPB_SIZE(56, 80), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {3, UPB_SIZE(32, 32), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 8), 2, 0, 4, kUpb_FieldMode_Scalar | (upb_FieldRep_8Byte << upb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 16), 3, 0, 3, kUpb_FieldMode_Scalar | (upb_FieldRep_8Byte << upb_FieldRep_Shift)},
  {6, UPB_SIZE(24, 24), 4, 0, 1, kUpb_FieldMode_Scalar | (upb_FieldRep_8Byte << upb_FieldRep_Shift)},
  {7, UPB_SIZE(40, 48), 5, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {8, UPB_SIZE(48, 64), 6, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_UninterpretedOption_msginit = {
  &google_protobuf_UninterpretedOption_submsgs[0],
  &google_protobuf_UninterpretedOption__fields[0],
  UPB_SIZE(64, 96), 7, upb_ExtMode_NonExtendable, 0, 255, 0,
};

static const upb_MiniTable_Field google_protobuf_UninterpretedOption_NamePart__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {2, UPB_SIZE(1, 1), 2, 0, 8, kUpb_FieldMode_Scalar | (upb_FieldRep_1Byte << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_UninterpretedOption_NamePart_msginit = {
  NULL,
  &google_protobuf_UninterpretedOption_NamePart__fields[0],
  UPB_SIZE(16, 32), 2, upb_ExtMode_NonExtendable, 2, 255, 2,
};

static const upb_MiniTable_Sub google_protobuf_SourceCodeInfo_submsgs[1] = {
  {.submsg = &google_protobuf_SourceCodeInfo_Location_msginit},
};

static const upb_MiniTable_Field google_protobuf_SourceCodeInfo__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_SourceCodeInfo_msginit = {
  &google_protobuf_SourceCodeInfo_submsgs[0],
  &google_protobuf_SourceCodeInfo__fields[0],
  UPB_SIZE(8, 8), 1, upb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Field google_protobuf_SourceCodeInfo_Location__fields[5] = {
  {1, UPB_SIZE(20, 40), 0, 0, 5, kUpb_FieldMode_Array | upb_LabelFlags_IsPacked | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {2, UPB_SIZE(24, 48), 0, 0, 5, kUpb_FieldMode_Array | upb_LabelFlags_IsPacked | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 24), 2, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {6, UPB_SIZE(28, 56), 0, 0, 12, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_SourceCodeInfo_Location_msginit = {
  NULL,
  &google_protobuf_SourceCodeInfo_Location__fields[0],
  UPB_SIZE(32, 64), 5, upb_ExtMode_NonExtendable, 4, 255, 0,
};

static const upb_MiniTable_Sub google_protobuf_GeneratedCodeInfo_submsgs[1] = {
  {.submsg = &google_protobuf_GeneratedCodeInfo_Annotation_msginit},
};

static const upb_MiniTable_Field google_protobuf_GeneratedCodeInfo__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, kUpb_FieldMode_Array | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_GeneratedCodeInfo_msginit = {
  &google_protobuf_GeneratedCodeInfo_submsgs[0],
  &google_protobuf_GeneratedCodeInfo__fields[0],
  UPB_SIZE(8, 8), 1, upb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable_Field google_protobuf_GeneratedCodeInfo_Annotation__fields[4] = {
  {1, UPB_SIZE(20, 32), 0, 0, 5, kUpb_FieldMode_Array | upb_LabelFlags_IsPacked | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), 1, 0, 12, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 4), 2, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 8), 3, 0, 5, kUpb_FieldMode_Scalar | (upb_FieldRep_4Byte << upb_FieldRep_Shift)},
};

const upb_MiniTable google_protobuf_GeneratedCodeInfo_Annotation_msginit = {
  NULL,
  &google_protobuf_GeneratedCodeInfo_Annotation__fields[0],
  UPB_SIZE(24, 48), 4, upb_ExtMode_NonExtendable, 4, 255, 0,
};

static const upb_MiniTable *messages_layout[27] = {
  &google_protobuf_FileDescriptorSet_msginit,
  &google_protobuf_FileDescriptorProto_msginit,
  &google_protobuf_DescriptorProto_msginit,
  &google_protobuf_DescriptorProto_ExtensionRange_msginit,
  &google_protobuf_DescriptorProto_ReservedRange_msginit,
  &google_protobuf_ExtensionRangeOptions_msginit,
  &google_protobuf_FieldDescriptorProto_msginit,
  &google_protobuf_OneofDescriptorProto_msginit,
  &google_protobuf_EnumDescriptorProto_msginit,
  &google_protobuf_EnumDescriptorProto_EnumReservedRange_msginit,
  &google_protobuf_EnumValueDescriptorProto_msginit,
  &google_protobuf_ServiceDescriptorProto_msginit,
  &google_protobuf_MethodDescriptorProto_msginit,
  &google_protobuf_FileOptions_msginit,
  &google_protobuf_MessageOptions_msginit,
  &google_protobuf_FieldOptions_msginit,
  &google_protobuf_OneofOptions_msginit,
  &google_protobuf_EnumOptions_msginit,
  &google_protobuf_EnumValueOptions_msginit,
  &google_protobuf_ServiceOptions_msginit,
  &google_protobuf_MethodOptions_msginit,
  &google_protobuf_UninterpretedOption_msginit,
  &google_protobuf_UninterpretedOption_NamePart_msginit,
  &google_protobuf_SourceCodeInfo_msginit,
  &google_protobuf_SourceCodeInfo_Location_msginit,
  &google_protobuf_GeneratedCodeInfo_msginit,
  &google_protobuf_GeneratedCodeInfo_Annotation_msginit,
};

const upb_MiniTable_Enum google_protobuf_FieldDescriptorProto_Type_enuminit = {
  NULL,
  0x7fffeULL,
  0,
};

const upb_MiniTable_Enum google_protobuf_FieldDescriptorProto_Label_enuminit = {
  NULL,
  0xeULL,
  0,
};

const upb_MiniTable_Enum google_protobuf_FileOptions_OptimizeMode_enuminit = {
  NULL,
  0xeULL,
  0,
};

const upb_MiniTable_Enum google_protobuf_FieldOptions_CType_enuminit = {
  NULL,
  0x7ULL,
  0,
};

const upb_MiniTable_Enum google_protobuf_FieldOptions_JSType_enuminit = {
  NULL,
  0x7ULL,
  0,
};

const upb_MiniTable_Enum google_protobuf_MethodOptions_IdempotencyLevel_enuminit = {
  NULL,
  0x7ULL,
  0,
};

static const upb_MiniTable_Enum *enums_layout[6] = {
  &google_protobuf_FieldDescriptorProto_Type_enuminit,
  &google_protobuf_FieldDescriptorProto_Label_enuminit,
  &google_protobuf_FileOptions_OptimizeMode_enuminit,
  &google_protobuf_FieldOptions_CType_enuminit,
  &google_protobuf_FieldOptions_JSType_enuminit,
  &google_protobuf_MethodOptions_IdempotencyLevel_enuminit,
};

const upb_MiniTable_File google_protobuf_descriptor_proto_upb_file_layout = {
  messages_layout,
  enums_layout,
  NULL,
  27,
  6,
  0,
};

#include "upb/port_undef.inc"

