/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/annotations/resource.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/annotations/resource.upb.h"
#include "google/protobuf/descriptor.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout_field envoy_annotations_ResourceAnnotation__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR | (_UPB_REP_STRVIEW << _UPB_REP_SHIFT)},
};

const upb_msglayout envoy_annotations_ResourceAnnotation_msginit = {
  NULL,
  &envoy_annotations_ResourceAnnotation__fields[0],
  UPB_SIZE(8, 16), 1, _UPB_MSGEXT_NONE, 1, 255,
};

static const upb_msglayout *messages_layout[1] = {
  &envoy_annotations_ResourceAnnotation_msginit,
};

extern const upb_msglayout envoy_annotations_ResourceAnnotation_msginit;
extern const upb_msglayout google_protobuf_ServiceOptions_msginit;
const upb_msglayout_ext envoy_annotations_resource_ext = {
  {265073217, 0, 0, 0, 11, _UPB_MODE_SCALAR | _UPB_MODE_IS_EXTENSION | (_UPB_REP_PTR << _UPB_REP_SHIFT)},
  &google_protobuf_ServiceOptions_msginit,
  {.submsg = &envoy_annotations_ResourceAnnotation_msginit},

};

static const upb_msglayout_ext *extensions_layout[1] = {
  &envoy_annotations_resource_ext,
};

const upb_msglayout_file envoy_annotations_resource_proto_upb_file_layout = {
  messages_layout,
  extensions_layout,
  1,
  1,
};

#include "upb/port_undef.inc"

