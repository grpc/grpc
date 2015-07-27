#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <stdlib.h>

#ifndef ENDOSCOPE_BACKEND_H_
#define ENDOSCOPE_BACKEND_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Data types */

#define ENDO_MUTEX_TYPE gpr_mu
#define ENDO_INT64 gpr_int64
#define ENDO_INDEX unsigned short
#define ENDO_EMPTY (ENDO_INDEX)(-1)

/* Public Macros */

#define ENDOSCOPE_INIT(base) endoscope_init(base)
#define ENDOSCOPE_DESTROY(base) endoscope_destroy(base)
#define ENDOSCOPE_BEGIN(base, name) \
        do { \
          ENDO_INT64* temp_ = endoscope_begin(base, name, \
              __FILE__, __LINE__, __PRETTY_FUNCTION__); \
          *temp_ = ENDO_CYCLENOW(); \
        } while (0)
#define ENDOSCOPE_END(base, name) endoscope_end(base, name, ENDO_CYCLENOW())
#define ENDOSCOPE_EVENT(base, name) endoscope_event(base, name, \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, ENDO_CYCLENOW())
#define ENDOSCOPE_ERROR(base, name) endoscope_error(base, name, \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, ENDO_CYCLENOW())

/* Private macros */

#define ENDO_CYCLENOW() endoscope_cyclenow()
#define ENDO_SYNCCLOCK(cycle, time) endoscope_syncclock(cycle, time)
#define ENDO_GETTID() endoscope_gettid()
#define ENDO_MUTEX_INIT(mu) \
        do { \
          mu = (void *)malloc(sizeof(ENDO_MUTEX_TYPE)); \
          gpr_mu_init((ENDO_MUTEX_TYPE*)(mu)); \
        } while (0)
#define ENDO_MUTEX_DESTROY(mu) \
        do { \
          gpr_mu_destroy((ENDO_MUTEX_TYPE*)(mu)); \
          free(mu); \
        } while (0)
#define ENDO_LOCK(mu) gpr_mu_lock((ENDO_MUTEX_TYPE*)(mu))
#define ENDO_UNLOCK(mu) gpr_mu_unlock((ENDO_MUTEX_TYPE*)(mu))

/* Constant Parameters (max capacity 65534) */

#define hash_size 97
#define marker_capacity 10000
#define task_capacity 10000
#define atom_capacity 50000
#define thread_capacity 100

/* Types */

typedef struct /* EndoMarker */ {  /* 47/48 marker_id as array index */
  ENDO_INT64 cycle_created;  /* overflow after 83 years on 3.5GHz CPU */
  ENDO_INT64 timestamp;
  const char *name;
  const char *file;
  const char *function_name;
  int line;
  ENDO_INDEX next_marker;  /* for hash map collision */
  unsigned char type;  /* EndoMarkerPB::MarkerType */
} EndoMarker;

typedef struct /* EndoTask */ {  /* 32/32 */
  ENDO_INT64 cycle_begin;
  ENDO_INT64 cycle_end;
  unsigned int task_id;
  ENDO_INDEX marker_id;
  ENDO_INDEX thread_index;
  ENDO_INDEX log_head;
  ENDO_INDEX log_tail;
  ENDO_INDEX next_task;
  ENDO_INDEX next_taskwithatom;
  unsigned short scope_depth;  /* max depth 65535 */
} EndoTask;

typedef struct /* EndoAtom */ {  /* 13/16 */
  ENDO_INT64 cycle;
  ENDO_INDEX param;  /* marker_id if ECOPE_BEGIN/EVENT/ERROR */
  ENDO_INDEX next_atom;  /* for both task log and available atoms */
  unsigned char type;  /* EndoAtomPB::AtomType */
} EndoAtom;

typedef struct /* EndoThread */ {  /* 22/24 */
  ENDO_INT64 cycle_created;
  ENDO_INT64 timestamp;
  int thread_id;
  ENDO_INDEX task_active;
} EndoThread;

typedef struct /* EndoBase */ {  /* endoscope instance */
  EndoMarker marker_pool[marker_capacity + 1];
  ENDO_INDEX marker_count;  /* number of used markers */
  ENDO_INDEX marker_map[hash_size];  /* lookup table for used markers */

  EndoTask task_pool[task_capacity + 1];
  ENDO_INDEX task_stack;  /* next available task */
  ENDO_INDEX task_history_head;
  ENDO_INDEX task_history_tail;
  ENDO_INDEX task_withatom_head;
  ENDO_INDEX task_withatom_tail;
  unsigned int task_count;  /* for task_id only, old task may be deleted */

  EndoAtom atom_pool[atom_capacity];  /* available-stack */
  ENDO_INDEX atom_stack;  /* next available atom */

  EndoThread thread_pool[thread_capacity + 1];
  ENDO_INDEX thread_count;

  EndoMarker *marker_error;
  EndoTask *task_error;
  EndoThread *thread_error;

  char errormsg[25];

  ENDO_INT64 cycle_begin;
  ENDO_INT64 cycle_sync;
  double time_begin;
  double time_sync;

  void* mutex;  /* pointer to mutex for this instance */
} EndoBase;

/* function definition */

void endoscope_init(EndoBase *base);

void endoscope_destroy(EndoBase *base);

ENDO_INT64* endoscope_begin(EndoBase *base, const char *name,
    const char *file, const int line, const char* function_name);

void endoscope_end(EndoBase *base, const char *name, ENDO_INT64 cycle_end);

void endoscope_event(EndoBase *base, const char *name,
    const char *file, const int line, const char* function_name,
    ENDO_INT64 cycle_event);

void endoscope_error(EndoBase *base, const char *name,
    const char *file, const int line, const char* function_name,
    ENDO_INT64 cycle_event);

ENDO_INT64 endoscope_cyclenow();

void endoscope_syncclock(ENDO_INT64 *cycle, double *time);

int endoscope_gettid();

#ifdef __cplusplus
}
#endif

#endif  /* ENDOSCOPE_BACKEND_H_ */
