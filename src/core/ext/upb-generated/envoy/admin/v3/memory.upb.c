/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/admin/v3/memory.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/admin/v3/memory.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_field envoy_admin_v3_Memory__fields[6] = {
  {1, UPB_SIZE(0, 0), 0, 0, 4, _UPB_MODE_SCALAR | (_UPB_REP_8BYTE << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(8, 8), 0, 0, 4, _UPB_MODE_SCALAR | (_UPB_REP_8BYTE << _UPB_REP_SHIFT)},
  {3, UPB_SIZE(16, 16), 0, 0, 4, _UPB_MODE_SCALAR | (_UPB_REP_8BYTE << _UPB_REP_SHIFT)},
  {4, UPB_SIZE(24, 24), 0, 0, 4, _UPB_MODE_SCALAR | (_UPB_REP_8BYTE << _UPB_REP_SHIFT)},
  {5, UPB_SIZE(32, 32), 0, 0, 4, _UPB_MODE_SCALAR | (_UPB_REP_8BYTE << _UPB_REP_SHIFT)},
  {6, UPB_SIZE(40, 40), 0, 0, 4, _UPB_MODE_SCALAR | (_UPB_REP_8BYTE << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_admin_v3_Memory_msginit = {
  NULL,
  &envoy_admin_v3_Memory__fields[0],
  UPB_SIZE(48, 48), 6, _UPB_MSGEXT_NONE, 6, 255,
};

static const upb_msglayout *messages_layout[1] = {
  &envoy_admin_v3_Memory_msginit,
};

const upb_msglayout_file envoy_admin_v3_memory_proto_upb_file_layout = {
  messages_layout,
  NULL,
  1,
  0,
};

#include "upb/port_undef.inc"

