/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "udpa/annotations/status.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_submsgs[1] = {
  {.submsg = &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msg_init},
};

static const upb_MiniTableField envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig__fields[1] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msg_init = {
  &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_submsgs[0],
  &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_DataSource_msg_init},
};

static const upb_MiniTableField envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msg_init = {
  &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_submsgs[0],
  &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[2] = {
  &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_msg_init,
  &envoy_extensions_transport_sockets_tls_v3_SPIFFECertValidatorConfig_TrustDomain_msg_init,
};

const upb_MiniTableFile envoy_extensions_transport_sockets_tls_v3_tls_spiffe_validator_config_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  2,
  0,
  0,
};

#include "upb/port/undef.inc"

