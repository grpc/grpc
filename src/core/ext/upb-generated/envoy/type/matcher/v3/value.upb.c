/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/v3/value.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/type/matcher/v3/value.upb.h"
#include "envoy/type/matcher/v3/number.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub envoy_type_matcher_v3_ValueMatcher_submsgs[4] = {
  {.submsg = &envoy_type_matcher_v3_ValueMatcher_NullMatch_msginit},
  {.submsg = &envoy_type_matcher_v3_DoubleMatcher_msginit},
  {.submsg = &envoy_type_matcher_v3_StringMatcher_msginit},
  {.submsg = &envoy_type_matcher_v3_ListMatcher_msginit},
};

static const upb_MiniTable_Field envoy_type_matcher_v3_ValueMatcher__fields[6] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_type_matcher_v3_ValueMatcher_msginit = {
  &envoy_type_matcher_v3_ValueMatcher_submsgs[0],
  &envoy_type_matcher_v3_ValueMatcher__fields[0],
  UPB_SIZE(8, 24), 6, kUpb_ExtMode_NonExtendable, 6, 255, 0,
};

const upb_MiniTable envoy_type_matcher_v3_ValueMatcher_NullMatch_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 8), 0, kUpb_ExtMode_NonExtendable, 0, 255, 0,
};

static const upb_MiniTable_Sub envoy_type_matcher_v3_ListMatcher_submsgs[1] = {
  {.submsg = &envoy_type_matcher_v3_ValueMatcher_msginit},
};

static const upb_MiniTable_Field envoy_type_matcher_v3_ListMatcher__fields[1] = {
  {1, UPB_SIZE(4, 8), UPB_SIZE(-1, -1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_type_matcher_v3_ListMatcher_msginit = {
  &envoy_type_matcher_v3_ListMatcher_submsgs[0],
  &envoy_type_matcher_v3_ListMatcher__fields[0],
  UPB_SIZE(8, 24), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable *messages_layout[3] = {
  &envoy_type_matcher_v3_ValueMatcher_msginit,
  &envoy_type_matcher_v3_ValueMatcher_NullMatch_msginit,
  &envoy_type_matcher_v3_ListMatcher_msginit,
};

const upb_MiniTable_File envoy_type_matcher_v3_value_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  3,
  0,
  0,
};

#include "upb/port_undef.inc"

