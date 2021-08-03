/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     udpa/data/orca/v1/orca_load_report.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef UDPA_DATA_ORCA_V1_ORCA_LOAD_REPORT_PROTO_UPB_H_
#define UDPA_DATA_ORCA_V1_ORCA_LOAD_REPORT_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct udpa_data_orca_v1_OrcaLoadReport;
struct udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry;
struct udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry;
typedef struct udpa_data_orca_v1_OrcaLoadReport udpa_data_orca_v1_OrcaLoadReport;
typedef struct udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry;
typedef struct udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry;
extern const upb_msglayout udpa_data_orca_v1_OrcaLoadReport_msginit;
extern const upb_msglayout udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry_msginit;
extern const upb_msglayout udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry_msginit;


/* udpa.data.orca.v1.OrcaLoadReport */

UPB_INLINE udpa_data_orca_v1_OrcaLoadReport *udpa_data_orca_v1_OrcaLoadReport_new(upb_arena *arena) {
  return (udpa_data_orca_v1_OrcaLoadReport *)_upb_msg_new(&udpa_data_orca_v1_OrcaLoadReport_msginit, arena);
}
UPB_INLINE udpa_data_orca_v1_OrcaLoadReport *udpa_data_orca_v1_OrcaLoadReport_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  udpa_data_orca_v1_OrcaLoadReport *ret = udpa_data_orca_v1_OrcaLoadReport_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &udpa_data_orca_v1_OrcaLoadReport_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE udpa_data_orca_v1_OrcaLoadReport *udpa_data_orca_v1_OrcaLoadReport_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  udpa_data_orca_v1_OrcaLoadReport *ret = udpa_data_orca_v1_OrcaLoadReport_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &udpa_data_orca_v1_OrcaLoadReport_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *udpa_data_orca_v1_OrcaLoadReport_serialize(const udpa_data_orca_v1_OrcaLoadReport *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &udpa_data_orca_v1_OrcaLoadReport_msginit, arena, len);
}

UPB_INLINE double udpa_data_orca_v1_OrcaLoadReport_cpu_utilization(const udpa_data_orca_v1_OrcaLoadReport *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), double); }
UPB_INLINE double udpa_data_orca_v1_OrcaLoadReport_mem_utilization(const udpa_data_orca_v1_OrcaLoadReport *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), double); }
UPB_INLINE uint64_t udpa_data_orca_v1_OrcaLoadReport_rps(const udpa_data_orca_v1_OrcaLoadReport *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 16), uint64_t); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_has_request_cost(const udpa_data_orca_v1_OrcaLoadReport *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(24, 24)); }
UPB_INLINE size_t udpa_data_orca_v1_OrcaLoadReport_request_cost_size(const udpa_data_orca_v1_OrcaLoadReport *msg) {return _upb_msg_map_size(msg, UPB_SIZE(24, 24)); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_request_cost_get(const udpa_data_orca_v1_OrcaLoadReport *msg, upb_strview key, double *val) { return _upb_msg_map_get(msg, UPB_SIZE(24, 24), &key, 0, val, sizeof(*val)); }
UPB_INLINE const udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry* udpa_data_orca_v1_OrcaLoadReport_request_cost_next(const udpa_data_orca_v1_OrcaLoadReport *msg, size_t* iter) { return (const udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry*)_upb_msg_map_next(msg, UPB_SIZE(24, 24), iter); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_has_utilization(const udpa_data_orca_v1_OrcaLoadReport *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(28, 32)); }
UPB_INLINE size_t udpa_data_orca_v1_OrcaLoadReport_utilization_size(const udpa_data_orca_v1_OrcaLoadReport *msg) {return _upb_msg_map_size(msg, UPB_SIZE(28, 32)); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_utilization_get(const udpa_data_orca_v1_OrcaLoadReport *msg, upb_strview key, double *val) { return _upb_msg_map_get(msg, UPB_SIZE(28, 32), &key, 0, val, sizeof(*val)); }
UPB_INLINE const udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry* udpa_data_orca_v1_OrcaLoadReport_utilization_next(const udpa_data_orca_v1_OrcaLoadReport *msg, size_t* iter) { return (const udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry*)_upb_msg_map_next(msg, UPB_SIZE(28, 32), iter); }

UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_set_cpu_utilization(udpa_data_orca_v1_OrcaLoadReport *msg, double value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), double) = value;
}
UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_set_mem_utilization(udpa_data_orca_v1_OrcaLoadReport *msg, double value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), double) = value;
}
UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_set_rps(udpa_data_orca_v1_OrcaLoadReport *msg, uint64_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 16), uint64_t) = value;
}
UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_request_cost_clear(udpa_data_orca_v1_OrcaLoadReport *msg) { _upb_msg_map_clear(msg, UPB_SIZE(24, 24)); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_request_cost_set(udpa_data_orca_v1_OrcaLoadReport *msg, upb_strview key, double val, upb_arena *a) { return _upb_msg_map_set(msg, UPB_SIZE(24, 24), &key, 0, &val, sizeof(val), a); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_request_cost_delete(udpa_data_orca_v1_OrcaLoadReport *msg, upb_strview key) { return _upb_msg_map_delete(msg, UPB_SIZE(24, 24), &key, 0); }
UPB_INLINE udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry* udpa_data_orca_v1_OrcaLoadReport_request_cost_nextmutable(udpa_data_orca_v1_OrcaLoadReport *msg, size_t* iter) { return (udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry*)_upb_msg_map_next(msg, UPB_SIZE(24, 24), iter); }
UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_utilization_clear(udpa_data_orca_v1_OrcaLoadReport *msg) { _upb_msg_map_clear(msg, UPB_SIZE(28, 32)); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_utilization_set(udpa_data_orca_v1_OrcaLoadReport *msg, upb_strview key, double val, upb_arena *a) { return _upb_msg_map_set(msg, UPB_SIZE(28, 32), &key, 0, &val, sizeof(val), a); }
UPB_INLINE bool udpa_data_orca_v1_OrcaLoadReport_utilization_delete(udpa_data_orca_v1_OrcaLoadReport *msg, upb_strview key) { return _upb_msg_map_delete(msg, UPB_SIZE(28, 32), &key, 0); }
UPB_INLINE udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry* udpa_data_orca_v1_OrcaLoadReport_utilization_nextmutable(udpa_data_orca_v1_OrcaLoadReport *msg, size_t* iter) { return (udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry*)_upb_msg_map_next(msg, UPB_SIZE(28, 32), iter); }

/* udpa.data.orca.v1.OrcaLoadReport.RequestCostEntry */

UPB_INLINE upb_strview udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry_key(const udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry *msg) {
  upb_strview ret;
  _upb_msg_map_key(msg, &ret, 0);
  return ret;
}
UPB_INLINE double udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry_value(const udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry *msg) {
  double ret;
  _upb_msg_map_value(msg, &ret, sizeof(ret));
  return ret;
}

UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry_set_value(udpa_data_orca_v1_OrcaLoadReport_RequestCostEntry *msg, double value) {
  _upb_msg_map_set_value(msg, &value, sizeof(double));
}

/* udpa.data.orca.v1.OrcaLoadReport.UtilizationEntry */

UPB_INLINE upb_strview udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry_key(const udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry *msg) {
  upb_strview ret;
  _upb_msg_map_key(msg, &ret, 0);
  return ret;
}
UPB_INLINE double udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry_value(const udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry *msg) {
  double ret;
  _upb_msg_map_value(msg, &ret, sizeof(ret));
  return ret;
}

UPB_INLINE void udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry_set_value(udpa_data_orca_v1_OrcaLoadReport_UtilizationEntry *msg, double value) {
  _upb_msg_map_set_value(msg, &value, sizeof(double));
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* UDPA_DATA_ORCA_V1_ORCA_LOAD_REPORT_PROTO_UPB_H_ */
