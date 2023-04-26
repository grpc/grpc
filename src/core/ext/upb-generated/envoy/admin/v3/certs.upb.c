/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/admin/v3/certs.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/admin/v3/certs.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_admin_v3_Certificates_submsgs[1] = {
  {.submsg = &envoy_admin_v3_Certificate_msg_init},
};

static const upb_MiniTableField envoy_admin_v3_Certificates__fields[1] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_admin_v3_Certificates_msg_init = {
  &envoy_admin_v3_Certificates_submsgs[0],
  &envoy_admin_v3_Certificates__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_admin_v3_Certificate_submsgs[2] = {
  {.submsg = &envoy_admin_v3_CertificateDetails_msg_init},
  {.submsg = &envoy_admin_v3_CertificateDetails_msg_init},
};

static const upb_MiniTableField envoy_admin_v3_Certificate__fields[2] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_admin_v3_Certificate_msg_init = {
  &envoy_admin_v3_Certificate_submsgs[0],
  &envoy_admin_v3_Certificate__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max128b},
    {0x000800003f010012, &upb_prm_1bt_max128b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_admin_v3_CertificateDetails_submsgs[4] = {
  {.submsg = &envoy_admin_v3_SubjectAlternateName_msg_init},
  {.submsg = &google_protobuf_Timestamp_msg_init},
  {.submsg = &google_protobuf_Timestamp_msg_init},
  {.submsg = &envoy_admin_v3_CertificateDetails_OcspDetails_msg_init},
};

static const upb_MiniTableField envoy_admin_v3_CertificateDetails__fields[7] = {
  {1, UPB_SIZE(20, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(28, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 40), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(40, 48), 0, kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(8, 56), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 64), 2, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 72), 3, 3, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_admin_v3_CertificateDetails_msg_init = {
  &envoy_admin_v3_CertificateDetails_submsgs[0],
  &envoy_admin_v3_CertificateDetails__fields[0],
  UPB_SIZE(48, 80), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800003f00001a, &upb_prm_1bt_max64b},
    {0x003000003f000020, &upb_psv8_1bt},
    {0x003800000101002a, &upb_psm_1bt_maxmaxb},
    {0x0040000002020032, &upb_psm_1bt_maxmaxb},
    {0x004800000303003a, &upb_psm_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_admin_v3_CertificateDetails_OcspDetails_submsgs[2] = {
  {.submsg = &google_protobuf_Timestamp_msg_init},
  {.submsg = &google_protobuf_Timestamp_msg_init},
};

static const upb_MiniTableField envoy_admin_v3_CertificateDetails_OcspDetails__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_admin_v3_CertificateDetails_OcspDetails_msg_init = {
  &envoy_admin_v3_CertificateDetails_OcspDetails_submsgs[0],
  &envoy_admin_v3_CertificateDetails_OcspDetails__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x0010000002010012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_admin_v3_SubjectAlternateName__fields[3] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_admin_v3_SubjectAlternateName_msg_init = {
  NULL,
  &envoy_admin_v3_SubjectAlternateName__fields[0],
  UPB_SIZE(16, 24), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000012, &upb_pos_1bt},
    {0x000800000300001a, &upb_pos_1bt},
  })
};

static const upb_MiniTable *messages_layout[5] = {
  &envoy_admin_v3_Certificates_msg_init,
  &envoy_admin_v3_Certificate_msg_init,
  &envoy_admin_v3_CertificateDetails_msg_init,
  &envoy_admin_v3_CertificateDetails_OcspDetails_msg_init,
  &envoy_admin_v3_SubjectAlternateName_msg_init,
};

const upb_MiniTableFile envoy_admin_v3_certs_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  5,
  0,
  0,
};

#include "upb/port/undef.inc"

