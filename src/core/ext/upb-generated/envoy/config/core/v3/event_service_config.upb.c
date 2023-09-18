/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/event_service_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/event_service_config.upb.h"
#include "envoy/config/core/v3/grpc_service.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_core_v3_EventServiceConfig_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_GrpcService_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_EventServiceConfig__fields[1] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_EventServiceConfig_msg_init = {
  &envoy_config_core_v3_EventServiceConfig_submsgs[0],
  &envoy_config_core_v3_EventServiceConfig__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_maxmaxb},
  })
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_config_core_v3_EventServiceConfig_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_event_service_config_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port/undef.inc"

