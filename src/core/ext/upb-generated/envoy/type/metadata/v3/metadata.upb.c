/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/metadata/v3/metadata.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/type/metadata/v3/metadata.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_sub envoy_type_metadata_v3_MetadataKey_submsgs[1] = {
  {.submsg = &envoy_type_metadata_v3_MetadataKey_PathSegment_msginit},
};

static const upb_msglayout_field envoy_type_metadata_v3_MetadataKey__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR | (_UPB_REP_STRVIEW << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(8, 16), 0, 0, 11, _UPB_MODE_ARRAY | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_type_metadata_v3_MetadataKey_msginit = {
  &envoy_type_metadata_v3_MetadataKey_submsgs[0],
  &envoy_type_metadata_v3_MetadataKey__fields[0],
  UPB_SIZE(16, 32), 2, _UPB_MSGEXT_NONE, 2, 255,
};

static const upb_msglayout_field envoy_type_metadata_v3_MetadataKey_PathSegment__fields[1] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 9, _UPB_MODE_SCALAR | (_UPB_REP_STRVIEW << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_type_metadata_v3_MetadataKey_PathSegment_msginit = {
  NULL,
  &envoy_type_metadata_v3_MetadataKey_PathSegment__fields[0],
  UPB_SIZE(16, 32), 1, _UPB_MSGEXT_NONE, 1, 255,
};

static const upb_msglayout_sub envoy_type_metadata_v3_MetadataKind_submsgs[4] = {
  {.submsg = &envoy_type_metadata_v3_MetadataKind_Cluster_msginit},
  {.submsg = &envoy_type_metadata_v3_MetadataKind_Host_msginit},
  {.submsg = &envoy_type_metadata_v3_MetadataKind_Request_msginit},
  {.submsg = &envoy_type_metadata_v3_MetadataKind_Route_msginit},
};

static const upb_msglayout_field envoy_type_metadata_v3_MetadataKind__fields[4] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(-5, -9), 2, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {2, UPB_SIZE(0, 0), UPB_SIZE(-5, -9), 3, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {3, UPB_SIZE(0, 0), UPB_SIZE(-5, -9), 0, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  {4, UPB_SIZE(0, 0), UPB_SIZE(-5, -9), 1, 11, _UPB_MODE_SCALAR | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_type_metadata_v3_MetadataKind_msginit = {
  &envoy_type_metadata_v3_MetadataKind_submsgs[0],
  &envoy_type_metadata_v3_MetadataKind__fields[0],
  UPB_SIZE(8, 16), 4, _UPB_MSGEXT_NONE, 4, 255,
};

const upb_msglayout envoy_type_metadata_v3_MetadataKind_Request_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, _UPB_MSGEXT_NONE, 0, 255,
};

const upb_msglayout envoy_type_metadata_v3_MetadataKind_Route_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, _UPB_MSGEXT_NONE, 0, 255,
};

const upb_msglayout envoy_type_metadata_v3_MetadataKind_Cluster_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, _UPB_MSGEXT_NONE, 0, 255,
};

const upb_msglayout envoy_type_metadata_v3_MetadataKind_Host_msginit = {
  NULL,
  NULL,
  UPB_SIZE(0, 0), 0, _UPB_MSGEXT_NONE, 0, 255,
};

static const upb_msglayout *messages_layout[7] = {
  &envoy_type_metadata_v3_MetadataKey_msginit,
  &envoy_type_metadata_v3_MetadataKey_PathSegment_msginit,
  &envoy_type_metadata_v3_MetadataKind_msginit,
  &envoy_type_metadata_v3_MetadataKind_Request_msginit,
  &envoy_type_metadata_v3_MetadataKind_Route_msginit,
  &envoy_type_metadata_v3_MetadataKind_Cluster_msginit,
  &envoy_type_metadata_v3_MetadataKind_Host_msginit,
};

const upb_msglayout_file envoy_type_metadata_v3_metadata_proto_upb_file_layout = {
  messages_layout,
  NULL,
  7,
  0,
};

#include "upb/port_undef.inc"

