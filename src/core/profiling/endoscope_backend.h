#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <stdlib.h>

#ifndef GRPC_CORE_PROFILING_ENDOSCOPE_BACKEND_H
#define GRPC_CORE_PROFILING_ENDOSCOPE_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Data types */

#define GRPC_ENDO_INDEX gpr_uint16
#define GRPC_ENDO_EMPTY (GRPC_ENDO_INDEX)(-1)

/* Public Macros */

#define GRPC_ENDO_INIT(base) grpc_endo_init(base)
#define GRPC_ENDO_DESTROY(base) grpc_endo_destroy(base)
#define GRPC_ENDO_BEGIN(base, name) \
        do { \
          gpr_int64* temp_ = grpc_endo_begin(base, name, \
              __FILE__, __LINE__, __PRETTY_FUNCTION__); \
          *temp_ = grpc_endo_cyclenow(); \
        } while (0)
#define GRPC_ENDO_END(base, name) grpc_endo_end(base, name, grpc_endo_cyclenow())
#define GRPC_ENDO_EVENT(base, name) grpc_endo_event(base, name, \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, grpc_endo_cyclenow())
#define GRPC_ENDO_ERROR(base, name) grpc_endo_error(base, name, \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, grpc_endo_cyclenow())

/* Constant Parameters (max capacity 65534) */

#define GRPC_ENDO_HASHSIZE 97
#define GRPC_ENDO_MARKER_CAPACITY 10000
#define GRPC_ENDO_TASK_CAPACITY 10000
#define GRPC_ENDO_ATOM_CAPACITY 50000
#define GRPC_ENDO_THREAD_CAPACITY 100

/* Types */

typedef struct grpc_endo_marker {
  /* marker_id as array index */
  gpr_int64 cycle_created;  /* overflow after 83 years on 3.5GHz CPU */
  gpr_int64 timestamp;
  const char *name;
  const char *file;
  const char *function_name;
  gpr_int32 line;
  GRPC_ENDO_INDEX next_marker;  /* for hash map collision */
  gpr_uint8 type;  /* EndoMarkerPB::MarkerType */
} grpc_endo_marker;

typedef struct grpc_endo_task {
  gpr_int64 cycle_begin;
  gpr_int64 cycle_end;
  gpr_uint32 task_id;
  GRPC_ENDO_INDEX marker_id;
  GRPC_ENDO_INDEX thread_index;
  GRPC_ENDO_INDEX log_head;
  GRPC_ENDO_INDEX log_tail;
  GRPC_ENDO_INDEX next_task;
  GRPC_ENDO_INDEX next_taskwithatom;
  gpr_uint16 scope_depth;  /* max depth 65535 */
} grpc_endo_task;

typedef struct grpc_endo_atom {
  gpr_int64 cycle;
  GRPC_ENDO_INDEX param;  /* marker_id if ECOPE_BEGIN/EVENT/ERROR */
  GRPC_ENDO_INDEX next_atom;  /* for both task log and available atoms */
  gpr_uint8 type;  /* EndoAtomPB::AtomType */
} grpc_endo_atom;

typedef struct grpc_endo_thread {
  gpr_int64 cycle_created;
  gpr_int64 timestamp;
  gpr_int32 thread_id;
  GRPC_ENDO_INDEX task_active;
} grpc_endo_thread;

typedef struct grpc_endo_base {  /* endoscope instance */
  grpc_endo_marker marker_pool[GRPC_ENDO_MARKER_CAPACITY + 1];
  GRPC_ENDO_INDEX marker_count;  /* number of used markers */
  GRPC_ENDO_INDEX marker_map[GRPC_ENDO_HASHSIZE];  /* lookup table for used markers */

  grpc_endo_task task_pool[GRPC_ENDO_TASK_CAPACITY + 1];
  GRPC_ENDO_INDEX task_stack;  /* next available task */
  GRPC_ENDO_INDEX task_history_head;
  GRPC_ENDO_INDEX task_history_tail;
  GRPC_ENDO_INDEX task_withatom_head;
  GRPC_ENDO_INDEX task_withatom_tail;
  gpr_uint32 task_count;  /* for task_id only, old task may be deleted */

  grpc_endo_atom atom_pool[GRPC_ENDO_ATOM_CAPACITY];  /* available-stack */
  GRPC_ENDO_INDEX atom_stack;  /* next available atom */

  grpc_endo_thread thread_pool[GRPC_ENDO_THREAD_CAPACITY + 1];
  GRPC_ENDO_INDEX thread_count;

  grpc_endo_marker *marker_warning;
  grpc_endo_task *task_warning;
  grpc_endo_thread *thread_warning;

  char warning_msg[25];

  gpr_int64 cycle_begin;
  gpr_int64 cycle_sync;
  double time_begin;
  double time_sync;

  gpr_mu mutex;
} grpc_endo_base;

/* function definition */

void grpc_endo_init(grpc_endo_base *base);

void grpc_endo_destroy(grpc_endo_base *base);

gpr_int64* grpc_endo_begin(grpc_endo_base *base, const char *name,
    const char *file, const gpr_int32 line, const char* function_name);

void grpc_endo_end(grpc_endo_base *base, const char *name, gpr_int64 cycle_end);

void grpc_endo_event(grpc_endo_base *base, const char *name,
    const char *file, const gpr_int32 line, const char* function_name,
    gpr_int64 cycle_event);

void grpc_endo_error(grpc_endo_base *base, const char *name,
    const char *file, const gpr_int32 line, const char* function_name,
    gpr_int64 cycle_event);

gpr_int64 grpc_endo_cyclenow();

void grpc_endo_syncclock(gpr_int64 *cycle, double *time);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_PROFILING_ENDOSCOPE_BACKEND_H */
