/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/proxy_protocol.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/core/v3/proxy_protocol.upb.h"
#include "udpa/annotations/status.upb.h"

#include "upb/port_def.inc"

static const upb_MiniTable_Field envoy_config_core_v3_ProxyProtocolConfig__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(0, 0), kUpb_NoSub, 5, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_ProxyProtocolConfig_msginit = {
  NULL,
  &envoy_config_core_v3_ProxyProtocolConfig__fields[0],
  UPB_SIZE(8, 8), 1, kUpb_ExtMode_NonExtendable, 1, 255, 0,
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_config_core_v3_ProxyProtocolConfig_msginit,
};

const upb_MiniTable_File envoy_config_core_v3_proxy_protocol_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port_undef.inc"

