/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/admin/v3/metrics.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/admin/v3/metrics.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Field envoy_admin_v3_SimpleMetric__fields[3] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), UPB_SIZE(0, 0), kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), UPB_SIZE(0, 0), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_admin_v3_SimpleMetric_msginit = {
  NULL,
  &envoy_admin_v3_SimpleMetric__fields[0],
  UPB_SIZE(24, 32), 3, kUpb_ExtMode_NonExtendable, 3, 255, 0,
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_admin_v3_SimpleMetric_msginit,
};

const upb_MiniTable_File envoy_admin_v3_metrics_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port_undef.inc"

