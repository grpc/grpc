/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/annotations/deprecation.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "envoy/annotations/deprecation.upb.h"
#include "google/protobuf/descriptor.upb.h"

#include "upb/port_def.inc"

extern const upb_msglayout google_protobuf_EnumValueOptions_msginit;
extern const upb_msglayout google_protobuf_FieldOptions_msginit;
const upb_msglayout_ext envoy_annotations_disallowed_by_default_ext = {
  {189503207, 0, 0, 0, 8, _UPB_MODE_SCALAR | _UPB_MODE_IS_EXTENSION | (_UPB_REP_1BYTE << _UPB_REP_SHIFT)},
  &google_protobuf_FieldOptions_msginit,
  {.submsg = NULL},

};
const upb_msglayout_ext envoy_annotations_deprecated_at_minor_version_ext = {
  {157299826, 0, 0, 0, 9, _UPB_MODE_SCALAR | _UPB_MODE_IS_EXTENSION | (_UPB_REP_STRVIEW << _UPB_REP_SHIFT)},
  &google_protobuf_FieldOptions_msginit,
  {.submsg = NULL},

};
const upb_msglayout_ext envoy_annotations_disallowed_by_default_enum_ext = {
  {70100853, 0, 0, 0, 8, _UPB_MODE_SCALAR | _UPB_MODE_IS_EXTENSION | (_UPB_REP_1BYTE << _UPB_REP_SHIFT)},
  &google_protobuf_EnumValueOptions_msginit,
  {.submsg = NULL},

};
const upb_msglayout_ext envoy_annotations_deprecated_at_minor_version_enum_ext = {
  {181198657, 0, 0, 0, 9, _UPB_MODE_SCALAR | _UPB_MODE_IS_EXTENSION | (_UPB_REP_STRVIEW << _UPB_REP_SHIFT)},
  &google_protobuf_EnumValueOptions_msginit,
  {.submsg = NULL},

};

static const upb_msglayout_ext *extensions_layout[4] = {
  &envoy_annotations_disallowed_by_default_ext,
  &envoy_annotations_deprecated_at_minor_version_ext,
  &envoy_annotations_disallowed_by_default_enum_ext,
  &envoy_annotations_deprecated_at_minor_version_enum_ext,
};

const upb_msglayout_file envoy_annotations_deprecation_proto_upb_file_layout = {
  NULL,
  extensions_layout,
  0,
  4,
};

#include "upb/port_undef.inc"

