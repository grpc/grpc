/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/resource.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "xds/core/v3/resource.upb.h"
#include "google/protobuf/any.upb.h"
#include "xds/annotations/v3/status.upb.h"
#include "xds/core/v3/resource_name.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub xds_core_v3_Resource_submsgs[2] = {
  {.submsg = &google_protobuf_Any_msginit},
  {.submsg = &xds_core_v3_ResourceName_msginit},
};

static const upb_MiniTable_Field xds_core_v3_Resource__fields[3] = {
  {1, UPB_SIZE(12, 24), 1, 1, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, 0, 9, kUpb_FieldMode_Scalar | (upb_FieldRep_StringView << upb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 32), 2, 0, 11, kUpb_FieldMode_Scalar | (upb_FieldRep_Pointer << upb_FieldRep_Shift)},
};

const upb_MiniTable xds_core_v3_Resource_msginit = {
  &xds_core_v3_Resource_submsgs[0],
  &xds_core_v3_Resource__fields[0],
  UPB_SIZE(24, 48), 3, upb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable *messages_layout[1] = {
  &xds_core_v3_Resource_msginit,
};

const upb_MiniTable_File xds_core_v3_resource_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port_undef.inc"

