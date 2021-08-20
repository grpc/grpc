/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/filters/http/router/v3/router.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/config/accesslog/v3/accesslog.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_extensions_filters_http_router_v3_Router_submsgs[2] = {
  &envoy_config_accesslog_v3_AccessLog_msginit,
  &google_protobuf_BoolValue_msginit,
};

static const upb_msglayout_field envoy_extensions_filters_http_router_v3_Router__fields[7] = {
  {1, UPB_SIZE(8, 8), 1, 1, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(1, 1), 0, 0, 8, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(12, 16), 0, 0, 11, _UPB_MODE_ARRAY},
  {4, UPB_SIZE(2, 2), 0, 0, 8, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(16, 24), 0, 0, 9, _UPB_MODE_ARRAY},
  {6, UPB_SIZE(3, 3), 0, 0, 8, _UPB_MODE_SCALAR},
  {7, UPB_SIZE(4, 4), 0, 0, 8, _UPB_MODE_SCALAR},
};

const upb_msglayout envoy_extensions_filters_http_router_v3_Router_msginit = {
  &envoy_extensions_filters_http_router_v3_Router_submsgs[0],
  &envoy_extensions_filters_http_router_v3_Router__fields[0],
  UPB_SIZE(24, 32), 7, false, 7, 255,
};

#include "upb/port_undef.inc"

