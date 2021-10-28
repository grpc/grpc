/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/resource_locator.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "xds/core/v3/resource_locator.upb.h"
#include "xds/annotations/v3/status.upb.h"
#include "xds/core/v3/context_params.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const xds_core_v3_ResourceLocator_submsgs[2] = {
  &xds_core_v3_ContextParams_msginit,
  &xds_core_v3_ResourceLocator_Directive_msginit,
};

static const upb_msglayout_field xds_core_v3_ResourceLocator__fields[6] = {
  {1, UPB_SIZE(0, 0), 0, 0, 14, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(4, 8), 0, 0, 9, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(12, 24), 0, 0, 9, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(20, 40), 0, 0, 9, _UPB_MODE_SCALAR},
  {5, UPB_SIZE(32, 64), UPB_SIZE(-37, -73), 0, 11, _UPB_MODE_SCALAR},
  {6, UPB_SIZE(28, 56), 0, 1, 11, _UPB_MODE_ARRAY},
};

const upb_msglayout xds_core_v3_ResourceLocator_msginit = {
  &xds_core_v3_ResourceLocator_submsgs[0],
  &xds_core_v3_ResourceLocator__fields[0],
  UPB_SIZE(40, 80), 6, false, 6, 255,
};

static const upb_msglayout *const xds_core_v3_ResourceLocator_Directive_submsgs[1] = {
  &xds_core_v3_ResourceLocator_msginit,
};

static const upb_msglayout_field xds_core_v3_ResourceLocator_Directive__fields[2] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 11, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 9, _UPB_MODE_SCALAR},
};

const upb_msglayout xds_core_v3_ResourceLocator_Directive_msginit = {
  &xds_core_v3_ResourceLocator_Directive_submsgs[0],
  &xds_core_v3_ResourceLocator_Directive__fields[0],
  UPB_SIZE(16, 32), 2, false, 2, 255,
};

#include "upb/port_undef.inc"

