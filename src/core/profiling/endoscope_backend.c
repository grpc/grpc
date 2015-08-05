#include "src/core/profiling/endoscope_backend.h"

#include <grpc/support/time.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <unistd.h>

static gpr_int64 grpc_endo_invalid64 = 0;  /* value written here will be ignored */

static const char *grpc_endo_warning_str = "MARKER Task Atom Thread ";
typedef enum grpc_endo_warning_enum {  /* values match positions */
  GRPC_ENDO_WARNING_MARKER = 0,
  GRPC_ENDO_WARNING_TASK = 7,
  GRPC_ENDO_WARNING_ATOM = 12,
  GRPC_ENDO_WARNING_THREAD = 17
} grpc_endo_warning_enum;

/* system related functions */

__inline gpr_int64 grpc_endo_cyclenow() {
#if defined(__i386__)
  gpr_int64 ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  return ret;
#elif defined(__x86_64__) || defined(__amd64__)
  unsigned long long low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return (high << 32) | low;
#else
#warning "Endoscope no rdtsc available."
  static gpr_int64 counter = 0;
  return ++counter;
#endif
}

void grpc_endo_syncclock(gpr_int64 *cycle, double *time) {
  gpr_timespec tv = gpr_now(GPR_CLOCK_REALTIME);
  *cycle = grpc_endo_cyclenow();
  *time = tv.tv_sec + (double)tv.tv_nsec / GPR_NS_PER_SEC;
}

/* warning span and warning print */

static void grpc_endo_warning_span(grpc_endo_base *base, grpc_endo_warning_enum errortype) {
  /* display to client */
  /* deterministic, no thread-safe issue */
  gpr_uint8 i;
  for (i = (gpr_uint8)errortype; grpc_endo_warning_str[i] != ' '; i++) {
    base->warning_msg[i] = grpc_endo_warning_str[i];
  }
  if (base->marker_warning->name == NULL) {
    gpr_int64 cycle_error = grpc_endo_cyclenow();
    base->marker_warning->cycle_created = cycle_error;
    base->task_warning->cycle_begin = cycle_error;
    base->thread_warning->cycle_created = cycle_error;
    base->marker_warning->name = base->warning_msg;
  }
}

static void grpc_endo_warning_print(const char *str) { /* print onto stderr */
  printf("### Endoscope grpc_endo_warning_print: %s\n", str);
}

static void grpc_endo_warning_print2(const char *str, const char *s1, const char *s2) {
  printf("### Endoscope grpc_endo_warning_print: %s (%s) (%s)\n", str, s1, s2);
}

/* Init */

void grpc_endo_init(grpc_endo_base *base) {
  /* must initialize before any BEGIN/END/EVENT, OK to initialize twice */
  /* deterministic, no thread safety issue with self, but unsafe with others */
  GRPC_ENDO_INDEX i;

  /* mutex_init */
  gpr_mu_init(&(base->mutex));

  /* instance-global variables */
  base->marker_count = 0;
  base->task_stack = 0;
  base->task_history_head = GRPC_ENDO_EMPTY;
  base->task_history_tail = GRPC_ENDO_EMPTY;
  base->task_withatom_head = GRPC_ENDO_EMPTY;
  base->task_withatom_tail = GRPC_ENDO_EMPTY;
  base->task_count = 0;
  base->atom_stack = 0;
  base->thread_count = 0;
  base->marker_warning = &(base->marker_pool[GRPC_ENDO_MARKER_CAPACITY]);
  base->task_warning = &(base->task_pool[GRPC_ENDO_TASK_CAPACITY]);
  base->thread_warning = &(base->thread_pool[GRPC_ENDO_THREAD_CAPACITY]);
  for (i = 0; i < sizeof(base->warning_msg); i++) {
    base->warning_msg[i] = ' ';
  }
  base->warning_msg[sizeof(base->warning_msg) - 1] = '\0';

  /* sync clock */
  grpc_endo_syncclock(&(base->cycle_sync), &(base->time_sync));
  base->cycle_begin = base->cycle_sync;
  base->time_begin = base->time_sync;

  /* arrays */
  for (i = 0; i < GRPC_ENDO_HASHSIZE; i++) {
    base->marker_map[i] = GRPC_ENDO_EMPTY;
  }
  for (i = 0; i < GRPC_ENDO_MARKER_CAPACITY; i++) {
    base->marker_pool[i].name = NULL;  /* as new */
    base->marker_pool[i].timestamp = 0;  /* important */
  }
  for (i = 0; i < GRPC_ENDO_ATOM_CAPACITY; i++) {
    base->atom_pool[i].next_atom = i + 1;
  }
  base->atom_pool[GRPC_ENDO_ATOM_CAPACITY - 1].next_atom = GRPC_ENDO_EMPTY;
  for (i = 0; i < GRPC_ENDO_TASK_CAPACITY; i++) {
    base->task_pool[i].next_task = i + 1;
    base->task_pool[i].next_taskwithatom = GRPC_ENDO_EMPTY;
    base->task_pool[i].log_head = GRPC_ENDO_EMPTY;
  }
  base->task_pool[GRPC_ENDO_TASK_CAPACITY - 1].next_task = GRPC_ENDO_EMPTY;
  for (i = 0; i < GRPC_ENDO_THREAD_CAPACITY; i++) {
    base->thread_pool[i].cycle_created = 0;  /* as new */
    base->thread_pool[i].timestamp = 0;  /* important */
  }

  /* error span */
  base->marker_warning->name = NULL;
  base->marker_warning->type = 2;  /* EndoMarkerPB::TASK */
  base->marker_warning->file = "ERROR";
  base->marker_warning->line = 0;
  base->marker_warning->function_name = "ERROR";
  base->marker_warning->timestamp = -1;
  base->marker_warning->next_marker = GRPC_ENDO_EMPTY;

  base->task_warning->task_id = 0x00ffffff;
  base->task_warning->marker_id = GRPC_ENDO_MARKER_CAPACITY;
  base->task_warning->thread_index = GRPC_ENDO_THREAD_CAPACITY;
  base->task_warning->cycle_end = -1;
  base->task_warning->log_head = GRPC_ENDO_EMPTY;
  base->task_warning->log_tail = GRPC_ENDO_EMPTY;
  base->task_warning->scope_depth = 0;
  base->task_warning->next_task = GRPC_ENDO_EMPTY;
  base->task_warning->next_taskwithatom = GRPC_ENDO_EMPTY;

  base->thread_warning->thread_id = 0;
  base->thread_warning->task_active = GRPC_ENDO_TASK_CAPACITY;
  base->thread_warning->timestamp = -1;
}

void grpc_endo_destroy(grpc_endo_base *base) {
  /* only free dynamic data to prevent leakage */
#ifdef GRPC_ENDO_COPY_MARKER_NAME
  GRPC_ENDO_INDEX i;
  for (i = 0; i < GRPC_ENDO_MARKER_CAPACITY; i++) {
    free(base->marker_pool[i].name);
  }
#endif
  gpr_mu_destroy(&(base->mutex));
}

/* Get, Create, and Delete Elements */

static GRPC_ENDO_INDEX grpc_endo_create_marker(grpc_endo_base *base, GRPC_ENDO_INDEX next_index) {
  /* must be called inside mutex */
  if (base->marker_count >= GRPC_ENDO_MARKER_CAPACITY) {
    grpc_endo_warning_print("grpc_endo_create_marker: no marker item available (reached capacity)");
    grpc_endo_warning_span(base, GRPC_ENDO_WARNING_MARKER);
    return GRPC_ENDO_EMPTY;
  } else {
    GRPC_ENDO_INDEX marker_id = base->marker_count++;
    /* base->marker_pool[marker_id].name = NULL already initialized */
    base->marker_pool[marker_id].next_marker = next_index;
    return marker_id;
  }
}

static GRPC_ENDO_INDEX grpc_endo_get_or_create_marker(
    grpc_endo_base *base, gpr_int32 line, const char *name) {
  /* thread safe applied */
  GRPC_ENDO_INDEX *q;
  gpr_int16 fallback;
  /* begin hash function */
  gpr_uint32 hash = line;
  const char *c;
  for (c = name; *c != '\0'; c++) {
    hash += *c;
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  /* end hash function */
  q = &(base->marker_map[hash % GRPC_ENDO_HASHSIZE]);
  for (fallback = 100; fallback > 1; fallback--) {
    if (*q == GRPC_ENDO_EMPTY) {  /* case 1: slot empty */
      gpr_mu_lock(&(base->mutex));
      if (*q == GRPC_ENDO_EMPTY) {  /* verify in lock - slot still empty */
        *q = grpc_endo_create_marker(base, GRPC_ENDO_EMPTY);  /* EMPTY-able*/
        gpr_mu_unlock(&(base->mutex));
        return *q;  /* EMPTY-able */
      } else {  /* fallback - slot no longer empty, something has occupied */
        gpr_mu_unlock(&(base->mutex));
        continue;  /* fallback to case 2 or 3 (case 1 guaranteed to fail)  */
      }
    } else {
      GRPC_ENDO_INDEX p = *q;
      for (; fallback > 1; fallback--) {
        while (p != GRPC_ENDO_EMPTY) {  /* case 2: between elements q and p */
          if (line == base->marker_pool[p].line) {  /* same line number */
            if (strcmp(name, base->marker_pool[p].name) == 0) {  /* also same string, found */
              return p;
            }
          } else if (line < base->marker_pool[p].line) {  /* ascending line number, insert */
            gpr_mu_lock(&(base->mutex));
            if (*q == p) {  /* verify in lock - still q->p */
              p = grpc_endo_create_marker(base, p);  /* EMPTY-able */
              if (p != GRPC_ENDO_EMPTY) {  /* insert only if non-EMPTY */
                *q = p;
              }
              gpr_mu_unlock(&(base->mutex));
              return p;  /* EMPTY-able */
            } else {  /* fallback - no longer q->p, something has inserted */
              gpr_mu_unlock(&(base->mutex));
              p = *q;
              continue;  /* fallback to case 2 between previous and new-inserted */
            }
          }
          q = &(base->marker_pool[p].next_marker);
          p = *q;
        }  /* case 3: q at the tail, *q == EMPTY now, append */
        gpr_mu_lock(&(base->mutex));
        if (*q == GRPC_ENDO_EMPTY) {  /* verify in lock - still tail */
          *q = grpc_endo_create_marker(base, GRPC_ENDO_EMPTY);  /* EMPTY-able */
          gpr_mu_unlock(&(base->mutex));
          return *q;
        } else {  /* fallback - something has appended */
          gpr_mu_unlock(&(base->mutex));
          p = *q;
          continue;  /* fallback to case 2 between previous tail and new-appended */
        }
      }
    }
    break;  /* can never fallback to case 1 */
  }
  return GRPC_ENDO_EMPTY;
}

static GRPC_ENDO_INDEX grpc_endo_get_or_create_thread(grpc_endo_base *base, gpr_uint64 thread_id) {
  /* thread safe applied */
  GRPC_ENDO_INDEX i;
  for (i = 0; i < base->thread_count; i++) {
    if (base->thread_pool[i].thread_id == thread_id) return i;  /* found */
  }
  gpr_mu_lock(&(base->mutex));
  if (base->thread_count >= GRPC_ENDO_THREAD_CAPACITY) {
    gpr_mu_unlock(&(base->mutex));
    grpc_endo_warning_print("grpc_endo_get_or_create_thread: no thread item available (reached capacity)");
    grpc_endo_warning_span(base, GRPC_ENDO_WARNING_THREAD);
    return GRPC_ENDO_EMPTY;
  } else {
    GRPC_ENDO_INDEX thread_index = base->thread_count++;
    gpr_mu_unlock(&(base->mutex));
    /* base->thread_pool[thread_index].cycle_created = 0 already initialized */
    return thread_index;
  }
}

static GRPC_ENDO_INDEX grpc_endo_get_thread(grpc_endo_base *base, gpr_uint64 thread_id) {
  /* extremely unlikely thread-safe issue */
  GRPC_ENDO_INDEX i;
  for (i = 0; i < base->thread_count; i++) {
    if (base->thread_pool[i].thread_id == thread_id) return i;
  }
  return GRPC_ENDO_EMPTY;
}

static void grpc_endo_recycle_atoms(grpc_endo_base *base) {
  /* must be called inside mutex */
  if (base->task_withatom_head != GRPC_ENDO_EMPTY) {
    GRPC_ENDO_INDEX task_index = base->task_withatom_head;
    grpc_endo_task *mytask = &(base->task_pool[task_index]);
    if (mytask->log_head == GRPC_ENDO_EMPTY) {
      grpc_endo_warning_print("grpc_endo_recycle_atoms: internal error log_head == EMPTY");
      return;
    }
    if (base->atom_stack != GRPC_ENDO_EMPTY) {  /* invalidate stack top before put atoms onto stack */
      base->atom_pool[base->atom_stack].type = 0;
    }
    /* begin transfer atoms to available stack */
    base->atom_pool[mytask->log_tail].next_atom = base->atom_stack;
    base->atom_stack = mytask->log_head;
    mytask->log_head = GRPC_ENDO_EMPTY;
    mytask->log_tail = GRPC_ENDO_EMPTY;
    /* end transfer atoms */
    base->task_withatom_head = mytask->next_taskwithatom;
    mytask->next_taskwithatom = GRPC_ENDO_EMPTY;
  } else {
    grpc_endo_warning_print("grpc_endo_recycle_atoms: no atom recyclable");
  }
}

static GRPC_ENDO_INDEX grpc_endo_create_atom(grpc_endo_base *base) {
  /* thread safe applied */
  GRPC_ENDO_INDEX atom_index;
  gpr_mu_lock(&(base->mutex));
  if (base->atom_stack == GRPC_ENDO_EMPTY) {
    grpc_endo_recycle_atoms(base);
    if (base->atom_stack == GRPC_ENDO_EMPTY) {  /* still empty, no atom gained by recycling */
      gpr_mu_unlock(&(base->mutex));
      grpc_endo_warning_print("grpc_endo_create_atom: no atom item available or recyclable to allocate");
      grpc_endo_warning_span(base, GRPC_ENDO_WARNING_ATOM);
      return GRPC_ENDO_EMPTY;
    }
  }
  atom_index = base->atom_stack;
  base->atom_stack = base->atom_pool[atom_index].next_atom;
  gpr_mu_unlock(&(base->mutex));
  base->atom_pool[atom_index].next_atom = GRPC_ENDO_EMPTY;
  return atom_index;
}

static GRPC_ENDO_INDEX grpc_endo_create_task(grpc_endo_base *base) {
  /* thread safe applied */
  GRPC_ENDO_INDEX task_index = GRPC_ENDO_EMPTY;
  gpr_mu_lock(&(base->mutex));
  if (base->task_stack != GRPC_ENDO_EMPTY) {  /* task item available */
    task_index = base->task_stack;
    base->task_stack = base->task_pool[task_index].next_task;
    base->task_pool[task_index].task_id = base->task_count++;
    gpr_mu_unlock(&(base->mutex));
  } else if (base->task_history_head != GRPC_ENDO_EMPTY) {  /* recycle oldest history task */
    task_index = base->task_history_head;
    if (base->task_withatom_head == task_index) {  /* if oldest history task has atoms then recycle */
      grpc_endo_recycle_atoms(base);
    }
    base->task_history_head = base->task_pool[task_index].next_task;
    base->task_pool[task_index].task_id = base->task_count++;
    gpr_mu_unlock(&(base->mutex));
  } else {  /* no available and cannot recycle */
    gpr_mu_unlock(&(base->mutex));
    grpc_endo_warning_print("grpc_endo_create_task: no task item available or recyclable to allocate");
    grpc_endo_warning_span(base, GRPC_ENDO_WARNING_TASK);
    return GRPC_ENDO_EMPTY;
  }
  base->task_pool[task_index].next_task = GRPC_ENDO_EMPTY;
  /* ASSERT base->task_pool[task_index].next_taskwithatom != EMPTY */
  return task_index;
}

/* Marker Implementation */

gpr_int64* grpc_endo_begin(grpc_endo_base *base, const char *name, const char *file, const gpr_int32 line,
    const char* function_name) {  /* return the position to set for cycle_now() */
  /* no thread safety needed assuming no conflict on the same thread */
  GRPC_ENDO_INDEX marker_id;
  gpr_uint64 thread_id;
  GRPC_ENDO_INDEX thread_index;
  grpc_endo_thread *mythread;
  gpr_int64 cycle_created = grpc_endo_cyclenow();

  marker_id = grpc_endo_get_or_create_marker(base, line, name);
  if (marker_id == GRPC_ENDO_EMPTY) {
    grpc_endo_warning_print("grpc_endo_begin: cannot create marker item");
    return &grpc_endo_invalid64;  /* only when out of markers */
  } else {
    grpc_endo_marker *mymarker = &(base->marker_pool[marker_id]);
    if (mymarker->name == NULL) {  /* created new marker if not found */
#ifdef GRPC_ENDO_COPY_MARKER_NAME
      mymarker->name = strdup(name);
#else
      mymarker->name = name;
#endif
      mymarker->type = 2;  /* EndoMarkerPB::TASK treat both task/scope markers as task */
      mymarker->file = file;
      mymarker->line = line;
      mymarker->function_name = function_name;
      mymarker->cycle_created = cycle_created;
    }
  }

  thread_id = gpr_thd_currentid();
  thread_index = grpc_endo_get_or_create_thread(base, thread_id);
  if (thread_index == GRPC_ENDO_EMPTY) {
    grpc_endo_warning_print("grpc_endo_begin: cannot create thread item");
    return &grpc_endo_invalid64;  /* only when out of threads */
  }
  mythread = &(base->thread_pool[thread_index]);
  if (mythread->cycle_created == 0) {  /* created new thread if not found */
    mythread->thread_id = thread_id;
    mythread->cycle_created = cycle_created;
    mythread->task_active = GRPC_ENDO_EMPTY;
  }

  if (mythread->task_active == GRPC_ENDO_EMPTY) {  /* top level as tasks */
    GRPC_ENDO_INDEX task_index = grpc_endo_create_task(base);
    if (task_index == GRPC_ENDO_EMPTY) {
      grpc_endo_warning_print("grpc_endo_begin: cannot create task item");
      return &grpc_endo_invalid64;  /* only when out of tasks */
    } else {
      grpc_endo_task *mytask = &(base->task_pool[task_index]);
      /* task_id already set in grpc_endo_create_task */
      mytask->marker_id = marker_id;
      mytask->thread_index = thread_index;
      mytask->cycle_end = -1;
      mytask->log_head = GRPC_ENDO_EMPTY;
      mytask->log_tail = GRPC_ENDO_EMPTY;
      mytask->scope_depth = 0;
      mytask->cycle_begin = cycle_created;  /* write something before attached to thread */
      /* ASSERT mytask->next_task = GRPC_ENDO_EMPTY */
      mythread->task_active = task_index;  /* attach to thread last */
      return &(mytask->cycle_begin);  /* immediately rewritten with cycle_now() after return */
    }
  } else {  /* lower lever as scope */
    grpc_endo_task *mytask = &(base->task_pool[mythread->task_active]);
    GRPC_ENDO_INDEX atom_index = grpc_endo_create_atom(base);
    mytask->scope_depth++;  /* no matter success or fail */
    if (atom_index == GRPC_ENDO_EMPTY) {
      grpc_endo_warning_print("grpc_endo_begin: cannot create atom item");
      return &grpc_endo_invalid64;  /* only when out of atoms */
    } else {
      grpc_endo_atom *myatom = &(base->atom_pool[atom_index]);
      myatom->type = 1;  /* EndoAtomPB::SCOPE_BEGIN */
      myatom->param = marker_id;
      myatom->cycle = cycle_created;  /* write something before attached to log */
      /* ASSERT myatom->next_atom = GRPC_ENDO_EMPTY */
      if (mytask->log_head == GRPC_ENDO_EMPTY) {
        mytask->log_head = atom_index;
      } else {
        base->atom_pool[mytask->log_tail].next_atom = atom_index;
      }
      mytask->log_tail = atom_index;
      return &(myatom->cycle);  /* immediately rewritten with cycle_now() after return */
    }
  }
}

void grpc_endo_end(grpc_endo_base *base, const char *name, gpr_int64 cycle_end) {
  /* thread safe applied */
  gpr_uint64 thread_id = gpr_thd_currentid();
  GRPC_ENDO_INDEX thread_index = grpc_endo_get_thread(base, thread_id);
  if (thread_index == GRPC_ENDO_EMPTY) {
    grpc_endo_warning_print("grpc_endo_end: cannot find thread item (begin-end mispair)");
    return;  /* only when thread invalid */
  } else {
    grpc_endo_thread *mythread = &(base->thread_pool[thread_index]);
    GRPC_ENDO_INDEX task_index = mythread->task_active;
    if (task_index == GRPC_ENDO_EMPTY) {
      grpc_endo_warning_print("grpc_endo_end: no active task on current thread (begin-end mispair)");
      return;  /* only when task invalid */
    } else {
      grpc_endo_task *mytask = &(base->task_pool[task_index]);
      if (mytask->scope_depth == 0) {  /* task complete */
        if (name != NULL) {  /* check marker name match */
          if (strcmp(name, base->marker_pool[mytask->marker_id].name) != 0) {
            grpc_endo_warning_print2("grpc_endo_end: begin-end mispair EndMarker != BeginMarker",
                    name, base->marker_pool[mytask->marker_id].name);
          }
        }
        mytask->cycle_end = cycle_end;
        gpr_mu_lock(&(base->mutex));  /* task_history and task_withatom must be in the same lock */
        if (base->task_history_head == GRPC_ENDO_EMPTY) {  /* attach to task_history */
          base->task_history_head = task_index;
        } else {
          base->task_pool[base->task_history_tail].next_task = task_index;
        }
        base->task_history_tail = task_index;
        /* at this intermediate state, task is both active and history to prevent red span */
        /* client can handle the duplication so no worry here */
        if (mytask->log_head != GRPC_ENDO_EMPTY) {  /* this task has atoms - attach to task_withatom */
          if (base->task_withatom_head == GRPC_ENDO_EMPTY) {
            base->task_withatom_head = task_index;
          } else {
            base->task_pool[base->task_withatom_tail].next_taskwithatom = task_index;
          }
          base->task_withatom_tail = task_index;
        }
        gpr_mu_unlock(&(base->mutex));
        mythread->task_active = GRPC_ENDO_EMPTY;  /* remove from thread's active-task */
      } else {  /* scope complete */
        GRPC_ENDO_INDEX atom_index = grpc_endo_create_atom(base);
        mytask->scope_depth--;  /* no matter success or fail */
        if (atom_index == GRPC_ENDO_EMPTY) {
          grpc_endo_warning_print("grpc_endo_end: cannot create atom item");
          return;  /* only when out of atoms */
        } else {
          grpc_endo_atom *myatom = &(base->atom_pool[atom_index]);
          myatom->cycle = cycle_end;
          myatom->type = 2;  /* EndoAtomPB::SCOPE_END */
          myatom->param = -1;
          if (mytask->log_head == GRPC_ENDO_EMPTY) {
            mytask->log_head = atom_index;
          } else {
            base->atom_pool[mytask->log_tail].next_atom = atom_index;
          }
          mytask->log_tail = atom_index;
        }
      }
    }
  }
}

static void grpc_endo_midpoint(grpc_endo_base *base, const char *name, const char *file, const gpr_int32 line,
    const char* function_name, gpr_int64 cycle_event, GRPC_ENDO_INDEX marker_type, GRPC_ENDO_INDEX atom_type) {
  GRPC_ENDO_INDEX marker_id;
  grpc_endo_marker *mymarker;
  gpr_uint64 thread_id;
  GRPC_ENDO_INDEX thread_index;
  grpc_endo_thread *mythread;

  marker_id = grpc_endo_get_or_create_marker(base, line, name);
  if (marker_id == GRPC_ENDO_EMPTY) {
    grpc_endo_warning_print("grpc_endo_midpoint: cannot create marker item");
    return;  /* only when out of markers */
  }

  mymarker = &(base->marker_pool[marker_id]);
  if (mymarker->name == NULL) {  /* created new marker if not found */
#ifdef GRPC_ENDO_COPY_MARKER_NAME
      mymarker->name = strdup(name);
#else
      mymarker->name = name;
#endif
    mymarker->type = marker_type;
    mymarker->file = file;
    mymarker->line = line;
    mymarker->function_name = function_name;
    mymarker->cycle_created = cycle_event;
  }

  thread_id = gpr_thd_currentid();
  thread_index = grpc_endo_get_thread(base, thread_id);
  if (thread_index == GRPC_ENDO_EMPTY) {
    grpc_endo_warning_print2("grpc_endo_midpoint cannot find thread item",
             "midpoint not in scope", mymarker->name);
    return;  /* only when thread invalid */
  }
  mythread = &(base->thread_pool[thread_index]);

  if (mythread->task_active == GRPC_ENDO_EMPTY) {  /* top level - invalid */
    grpc_endo_warning_print2("grpc_endo_midpoint no active task on current thread",
             "midpoint not in scope", mymarker->name);
    return;
  } else {  /* should be in task or scope */
    grpc_endo_task *mytask = &(base->task_pool[mythread->task_active]);
    GRPC_ENDO_INDEX atom_index = grpc_endo_create_atom(base);
    if (atom_index == GRPC_ENDO_EMPTY) {
      grpc_endo_warning_print("grpc_endo_midpoint cannot create atom item");
      return;  /* only when out of atoms */
    } else {
      grpc_endo_atom *myatom = &(base->atom_pool[atom_index]);
      myatom->cycle = cycle_event;
      myatom->type = atom_type;
      myatom->param = marker_id;
      if (mytask->log_head == GRPC_ENDO_EMPTY) {
        mytask->log_head = atom_index;
      } else {
        base->atom_pool[mytask->log_tail].next_atom = atom_index;
      }
      mytask->log_tail = atom_index;
    }
  }
}

void grpc_endo_event(grpc_endo_base *base, const char *name, const char *file, const gpr_int32 line,
                   const char* function_name, gpr_int64 cycle_event) {
  grpc_endo_midpoint(base, name, file, line, function_name, cycle_event,
                      3 /* EndoMarkerPB::EVENT */ , 
                      5 /* EndoAtomPB::EVENT */ );
}

void grpc_endo_error(grpc_endo_base *base, const char *name, const char *file, const gpr_int32 line,
                   const char* function_name, gpr_int64 cycle_event) {
  grpc_endo_midpoint(base, name, file, line, function_name, cycle_event,
                      4 /* EndoMarkerPB::ERROR */ , 
                      6 /* EndoAtomPB::ERROR */ );
}
