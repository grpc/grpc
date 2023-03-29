/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/common/v3/common.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "envoy/annotations/deprecation.upb.h"
#include "udpa/annotations/status.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Sub envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_submsgs[5] = {
  {.submsg = &google_protobuf_UInt64Value_msginit},
  {.submsg = &google_protobuf_UInt64Value_msginit},
  {.submsg = &google_protobuf_UInt32Value_msginit},
  {.submsg = &envoy_extensions_load_balancing_policies_common_v3_ConsistentHashingLbConfig_msginit},
  {.submsg = &envoy_extensions_load_balancing_policies_common_v3_LocalityLbConfig_LocalityWeightedLbConfig_msginit},
};

static const upb_MiniTable_Field envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash__fields[7] = {
  {1, UPB_SIZE(4, 4), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), UPB_SIZE(1, 1), 0, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 24), UPB_SIZE(2, 2), 1, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 8), UPB_SIZE(0, 0), kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 32), UPB_SIZE(3, 3), 2, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(24, 40), UPB_SIZE(4, 4), 3, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(28, 48), UPB_SIZE(5, 5), 4, 11, kUpb_FieldMode_Scalar | (kUpb_FieldRep_Pointer << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_msginit = {
  &envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_submsgs[0],
  &envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash__fields[0],
  UPB_SIZE(32, 56), 7, kUpb_ExtMode_NonExtendable, 7, 255, 0,
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_msginit,
};

const upb_MiniTable_File envoy_extensions_load_balancing_policies_ring_hash_v3_ring_hash_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port_undef.inc"

