/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/http_uri.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/config/core/v3/http_uri.upb.h"
#include "google/protobuf/duration.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_config_core_v3_HttpUri_submsgs[1] = {
  &google_protobuf_Duration_msginit,
};

static const upb_msglayout_field envoy_config_core_v3_HttpUri__fields[3] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(16, 32), UPB_SIZE(-25, -49), 0, 9, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(12, 24), 1, 0, 11, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_config_core_v3_HttpUri_msginit = {
  &envoy_config_core_v3_HttpUri_submsgs[0],
  &envoy_config_core_v3_HttpUri__fields[0],
  UPB_SIZE(32, 64), 3, false, 3, 255,
};

#include "upb/port_undef.inc"

