/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/data/orca/v3/orca_load_report.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg_internal.h"
#include "xds/data/orca/v3/orca_load_report.upb.h"
#include "validate/validate.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const xds_data_orca_v3_OrcaLoadReport_submsgs[2] = {
  &xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_msginit,
  &xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_msginit,
};

static const upb_msglayout_field xds_data_orca_v3_OrcaLoadReport__fields[5] = {
  {1, UPB_SIZE(0, 0), 0, 0, 1, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 8), 0, 0, 1, _UPB_MODE_SCALAR},
  {3, UPB_SIZE(16, 16), 0, 0, 4, _UPB_MODE_SCALAR},
  {4, UPB_SIZE(24, 24), 0, 0, 11, _UPB_MODE_MAP},
  {5, UPB_SIZE(28, 32), 0, 1, 11, _UPB_MODE_MAP},
};

const upb_msglayout xds_data_orca_v3_OrcaLoadReport_msginit = {
  &xds_data_orca_v3_OrcaLoadReport_submsgs[0],
  &xds_data_orca_v3_OrcaLoadReport__fields[0],
  UPB_SIZE(32, 40), 5, false, 5, 255,
};

static const upb_msglayout_field xds_data_orca_v3_OrcaLoadReport_RequestCostEntry__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 0, 0, 1, _UPB_MODE_SCALAR},
};

const upb_msglayout xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_msginit = {
  NULL,
  &xds_data_orca_v3_OrcaLoadReport_RequestCostEntry__fields[0],
  UPB_SIZE(16, 32), 2, false, 2, 255,
};

static const upb_msglayout_field xds_data_orca_v3_OrcaLoadReport_UtilizationEntry__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, _UPB_MODE_SCALAR},
  {2, UPB_SIZE(8, 16), 0, 0, 1, _UPB_MODE_SCALAR},
};

const upb_msglayout xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_msginit = {
  NULL,
  &xds_data_orca_v3_OrcaLoadReport_UtilizationEntry__fields[0],
  UPB_SIZE(16, 32), 2, false, 2, 255,
};

#include "upb/port_undef.inc"

