/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/number.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "envoy/type/matcher/number.upb.h"
#include "envoy/type/range.upb.h"
#include "udpa/annotations/status.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const envoy_type_matcher_DoubleMatcher_submsgs[1] = {
  &envoy_type_DoubleRange_msginit,
};

static const upb_msglayout_field envoy_type_matcher_DoubleMatcher__fields[2] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(-9, -9), 0, 11, 1},
  {2, UPB_SIZE(0, 0), UPB_SIZE(-9, -9), 0, 1, 1},
};

const upb_msglayout envoy_type_matcher_DoubleMatcher_msginit = {
  &envoy_type_matcher_DoubleMatcher_submsgs[0],
  &envoy_type_matcher_DoubleMatcher__fields[0],
  UPB_SIZE(16, 16), 2, false,
};

#include "upb/port_undef.inc"

