#include "src/core/profiling/endoscope_backend.h"

#include <grpc/support/time.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <unistd.h>

static ENDO_INT64 ENDO_INVALID64 = 0;  /* value written here will be ignored */

static const char *errorstr = "MARKER Task Atom Thread ";
typedef enum /* ErrorEnum */ {  /* values match positions */
  MARKER = 0,
  TASK = 7,
  ATOM = 12,
  THREAD = 17
} ErrorEnum;

/* system related functions */

ENDO_INT64 endoscope_cyclenow() {
#if defined(__i386__)
  ENDO_INT64 ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  return ret;
#elif defined(__x86_64__) || defined(__amd64__)
  unsigned long long low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return (high << 32) | low;
#else
#warning "Endoscope no rdtsc available."
  static ENDO_INT64 counter = 0;
  return ++counter;
#endif
}

void endoscope_syncclock(ENDO_INT64 *cycle, double *time) {
  gpr_timespec tv = gpr_now(GPR_CLOCK_REALTIME);
  *cycle = ENDO_CYCLENOW();
  *time = tv.tv_sec + (double)tv.tv_nsec / GPR_NS_PER_SEC;
}

int endoscope_gettid() {
#if defined(GPR_LINUX)
  return (int)syscall(__NR_gettid);  /* linux */
#elif defined(GPR_WIN32)
  return (int)GetCurrentThreadId();  /* win32 */
#elif defined(GPR_POSIX_LOG)
  return (int)pthread_self();  /* posix */
#else
#warning "Endoscope no thread id available."
  return -1;
#endif
}

/* Error & Warning (internal) */

static void Error(EndoBase *base, ErrorEnum errortype) { /* display to client */
  /* deterministic, no thread-safe issue */
  int i;
  for (i = (int)errortype; errorstr[i] != ' '; i++) {
    base->errormsg[i] = errorstr[i];
  }
  if (base->marker_error->name == NULL) {
    ENDO_INT64 cycle_error = ENDO_CYCLENOW();
    base->marker_error->cycle_created = cycle_error;
    base->task_error->cycle_begin = cycle_error;
    base->thread_error->cycle_created = cycle_error;
    base->marker_error->name = base->errormsg;
  }
}

static void Warning(const char *str) { /* print onto stderr */
  printf("### Endoscope Warning: %s\n", str);
}

static void Warning2(const char *str, const char *s1, const char *s2) {
  printf("### Endoscope Warning: %s (%s) (%s)\n", str, s1, s2);
}

/* Init */

void endoscope_init(EndoBase *base) {
  /* must initialize before any BEGIN/END/EVENT, OK to initialize twice */
  /* deterministic, no thread safety issue with self, but unsafe with others */
  ENDO_INDEX i;

  /* mutex_init */
  ENDO_MUTEX_INIT(base->mutex);

  /* instance-global variables */
  base->marker_count = 0;
  base->task_stack = 0;
  base->task_history_head = ENDO_EMPTY;
  base->task_history_tail = ENDO_EMPTY;
  base->task_withatom_head = ENDO_EMPTY;
  base->task_withatom_tail = ENDO_EMPTY;
  base->task_count = 0;
  base->atom_stack = 0;
  base->thread_count = 0;
  base->marker_error = &(base->marker_pool[marker_capacity]);
  base->task_error = &(base->task_pool[task_capacity]);
  base->thread_error = &(base->thread_pool[thread_capacity]);
  for (i = 0; i < sizeof(base->errormsg); i++) {
    base->errormsg[i] = ' ';
  }
  base->errormsg[sizeof(base->errormsg) - 1] = '\0';

  /* sync clock */
  ENDO_SYNCCLOCK(&(base->cycle_sync), &(base->time_sync));
  base->cycle_begin = base->cycle_sync;
  base->time_begin = base->time_sync;

  /* arrays */
  for (i = 0; i < hash_size; i++) {
    base->marker_map[i] = ENDO_EMPTY;
  }
  for (i = 0; i < marker_capacity; i++) {
    base->marker_pool[i].name = NULL;  /* as new */
    base->marker_pool[i].timestamp = 0;  /* important */
  }
  for (i = 0; i < atom_capacity; i++) {
    base->atom_pool[i].next_atom = i + 1;
  }
  base->atom_pool[atom_capacity - 1].next_atom = ENDO_EMPTY;
  for (i = 0; i < task_capacity; i++) {
    base->task_pool[i].next_task = i + 1;
    base->task_pool[i].next_taskwithatom = ENDO_EMPTY;
    base->task_pool[i].log_head = ENDO_EMPTY;
  }
  base->task_pool[task_capacity - 1].next_task = ENDO_EMPTY;
  for (i = 0; i < thread_capacity; i++) {
    base->thread_pool[i].cycle_created = 0;  /* as new */
    base->thread_pool[i].timestamp = 0;  /* important */
  }

  /* error span */
  base->marker_error->name = NULL;
  base->marker_error->type = 2;  /* EndoMarkerPB::TASK */
  base->marker_error->file = "ERROR";
  base->marker_error->line = 0;
  base->marker_error->function_name = "ERROR";
  base->marker_error->timestamp = -1;
  base->marker_error->next_marker = ENDO_EMPTY;

  base->task_error->task_id = 0x00ffffff;
  base->task_error->marker_id = marker_capacity;
  base->task_error->thread_index = thread_capacity;
  base->task_error->cycle_end = -1;
  base->task_error->log_head = ENDO_EMPTY;
  base->task_error->log_tail = ENDO_EMPTY;
  base->task_error->scope_depth = 0;
  base->task_error->next_task = ENDO_EMPTY;
  base->task_error->next_taskwithatom = ENDO_EMPTY;

  base->thread_error->thread_id = 0;
  base->thread_error->task_active = task_capacity;
  base->thread_error->timestamp = -1;
}

void endoscope_destroy(EndoBase *base) {
  /* only free dynamic data to prevent leakage */
#ifdef ENDOSCOPE_COPY_MARKER_NAME
  ENDO_INDEX i;
  for (i = 0; i < marker_capacity; i++) {
    free(base->marker_pool[i].name);
  }
#endif
  ENDO_MUTEX_DESTROY(base->mutex);
}

/* Get, Create, and Delete Elements */

static ENDO_INDEX CreateMarker(EndoBase *base, ENDO_INDEX next_index) {
  /* must be called inside mutex */
  if (base->marker_count >= marker_capacity) {
    Warning("CreateMarker: no marker item available (reached capacity)");
    Error(base, MARKER);
    return ENDO_EMPTY;
  } else {
    ENDO_INDEX marker_id = base->marker_count++;
    /* base->marker_pool[marker_id].name = NULL already initialized */
    base->marker_pool[marker_id].next_marker = next_index;
    return marker_id;
  }
}

static ENDO_INDEX GetOrCreateMarker(EndoBase *base, int line, const char *name) {
  /* thread safe applied */
  ENDO_INDEX *q;
  int fallback;
  /* begin hash function */
  unsigned int hash = line;
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
  q = &(base->marker_map[hash % hash_size]);
  for (fallback = 100; fallback > 0; fallback--) {
    if (*q == ENDO_EMPTY) {  /* case 1: slot empty */
      ENDO_LOCK(base->mutex);
      if (*q == ENDO_EMPTY) {  /* verify in lock - slot still empty */
        *q = CreateMarker(base, ENDO_EMPTY);  /* EMPTY-able*/
        ENDO_UNLOCK(base->mutex);
        return *q;  /* EMPTY-able */
      } else {  /* fallback - slot no longer empty, something has occupied */
        ENDO_UNLOCK(base->mutex);
        continue;  /* fallback to case 2 or 3 (case 1 guaranteed to fail)  */
      }
    } else {
      ENDO_INDEX p = *q;
      for (; fallback > 0; fallback--) {
        while (p != ENDO_EMPTY) {  /* case 2: between elements q and p */
          if (line == base->marker_pool[p].line) {  /* same line number */
            if (strcmp(name, base->marker_pool[p].name) == 0) {  /* also same string, found */
              return p;
            }
          } else if (line < base->marker_pool[p].line) {  /* ascending line number, insert */
            ENDO_LOCK(base->mutex);
            if (*q == p) {  /* verify in lock - still q->p */
              p = CreateMarker(base, p);  /* EMPTY-able */
              if (p != ENDO_EMPTY) {  /* insert only if non-EMPTY */
                *q = p;
              }
              ENDO_UNLOCK(base->mutex);
              return p;  /* EMPTY-able */
            } else {  /* fallback - no longer q->p, something has inserted */
              ENDO_UNLOCK(base->mutex);
              p = *q;
              continue;  /* fallback to case 2 between previous and new-inserted */
            }
          }
          q = &(base->marker_pool[p].next_marker);
          p = *q;
        }  /* case 3: q at the tail, *q == EMPTY now, append */
        ENDO_LOCK(base->mutex);
        if (*q == ENDO_EMPTY) {  /* verify in lock - still tail */
          *q = CreateMarker(base, ENDO_EMPTY);  /* EMPTY-able */
          ENDO_UNLOCK(base->mutex);
          return *q;
        } else {  /* fallback - something has appended */
          ENDO_UNLOCK(base->mutex);
          p = *q;
          continue;  /* fallback to case 2 between previous tail and new-appended */
        }
      }
    }
  }
  return ENDO_EMPTY;
}

static ENDO_INDEX GetOrCreateThread(EndoBase *base, int thread_id) {
  /* thread safe applied */
  ENDO_INDEX i;
  for (i = 0; i < base->thread_count; i++) {
    if (base->thread_pool[i].thread_id == thread_id) return i;  /* found */
  }
  ENDO_LOCK(base->mutex);
  if (base->thread_count >= thread_capacity) {
    ENDO_UNLOCK(base->mutex);
    Warning("GetOrCreateThread: no thread item available (reached capacity)");
    Error(base, THREAD);
    return ENDO_EMPTY;
  } else {
    ENDO_INDEX thread_index = base->thread_count++;
    ENDO_UNLOCK(base->mutex);
    /* base->thread_pool[thread_index].cycle_created = 0 already initialized */
    return thread_index;
  }
}

static ENDO_INDEX GetThread(EndoBase *base, int thread_id) {
  /* extremely unlikely thread-safe issue */
  ENDO_INDEX i;
  for (i = 0; i < base->thread_count; i++) {
    if (base->thread_pool[i].thread_id == thread_id) return i;
  }
  return ENDO_EMPTY;
}

static void RecycleAtoms(EndoBase *base) {
  /* must be called inside mutex */
  if (base->task_withatom_head != ENDO_EMPTY) {
    ENDO_INDEX task_index = base->task_withatom_head;
    EndoTask *mytask = &(base->task_pool[task_index]);
    if (mytask->log_head == ENDO_EMPTY) {
      Warning("RecycleAtoms: internal error log_head == EMPTY");
      return;
    }
    if (base->atom_stack != ENDO_EMPTY) {  /* invalidate stack top before put atoms onto stack */
      base->atom_pool[base->atom_stack].type = 0;
    }
    /* begin transfer atoms to available stack */
    base->atom_pool[mytask->log_tail].next_atom = base->atom_stack;
    base->atom_stack = mytask->log_head;
    mytask->log_head = ENDO_EMPTY;
    mytask->log_tail = ENDO_EMPTY;
    /* end transfer atoms */
    base->task_withatom_head = mytask->next_taskwithatom;
    mytask->next_taskwithatom = ENDO_EMPTY;
  } else {
    Warning("RecycleAtoms: no atom recyclable");
  }
}

static ENDO_INDEX CreateAtom(EndoBase *base) {
  /* thread safe applied */
  ENDO_INDEX atom_index;
  ENDO_LOCK(base->mutex);
  if (base->atom_stack == ENDO_EMPTY) {
    RecycleAtoms(base);
    if (base->atom_stack == ENDO_EMPTY) {  /* still empty, no atom gained by recycling */
      ENDO_UNLOCK(base->mutex);
      Warning("CreateAtom: no atom item available or recyclable to allocate");
      Error(base, ATOM);
      return ENDO_EMPTY;
    }
  }
  atom_index = base->atom_stack;
  base->atom_stack = base->atom_pool[atom_index].next_atom;
  ENDO_UNLOCK(base->mutex);
  base->atom_pool[atom_index].next_atom = ENDO_EMPTY;
  return atom_index;
}

static ENDO_INDEX CreateTask(EndoBase *base) {
  /* thread safe applied */
  ENDO_INDEX task_index = ENDO_EMPTY;
  ENDO_LOCK(base->mutex);
  if (base->task_stack != ENDO_EMPTY) {  /* task item available */
    task_index = base->task_stack;
    base->task_stack = base->task_pool[task_index].next_task;
    base->task_pool[task_index].task_id = base->task_count++;
    ENDO_UNLOCK(base->mutex);
  } else if (base->task_history_head != ENDO_EMPTY) {  /* recycle oldest history task */
    task_index = base->task_history_head;
    if (base->task_withatom_head == task_index) {  /* if oldest history task has atoms then recycle */
      RecycleAtoms(base);
    }
    base->task_history_head = base->task_pool[task_index].next_task;
    base->task_pool[task_index].task_id = base->task_count++;
    ENDO_UNLOCK(base->mutex);
  } else {  /* no available and cannot recycle */
    ENDO_UNLOCK(base->mutex);
    Warning("CreateTask: no task item available or recyclable to allocate");
    Error(base, TASK);
    return ENDO_EMPTY;
  }
  base->task_pool[task_index].next_task = ENDO_EMPTY;
  /* ASSERT base->task_pool[task_index].next_taskwithatom != EMPTY */
  return task_index;
}

/* Marker Implementation */

ENDO_INT64* endoscope_begin(EndoBase *base, const char *name, const char *file, const int line,
    const char* function_name) {  /* return the position to set for cycle_now() */
  /* no thread safety needed assuming no conflict on the same thread */
  ENDO_INDEX marker_id;
  int thread_id;
  ENDO_INDEX thread_index;
  EndoThread *mythread;
  ENDO_INT64 cycle_created = ENDO_CYCLENOW();

  marker_id = GetOrCreateMarker(base, line, name);
  if (marker_id == ENDO_EMPTY) {
    Warning("endoscope_begin: cannot create marker item");
    return &ENDO_INVALID64;  /* only when out of markers */
  } else {
    EndoMarker *mymarker = &(base->marker_pool[marker_id]);
    if (mymarker->name == NULL) {  /* created new marker if not found */
#ifdef ENDOSCOPE_COPY_MARKER_NAME
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

  thread_id = ENDO_GETTID();
  thread_index = GetOrCreateThread(base, thread_id);
  if (thread_index == ENDO_EMPTY) {
    Warning("endoscope_begin: cannot create thread item");
    return &ENDO_INVALID64;  /* only when out of threads */
  }
  mythread = &(base->thread_pool[thread_index]);
  if (mythread->cycle_created == 0) {  /* created new thread if not found */
    mythread->thread_id = thread_id;
    mythread->cycle_created = cycle_created;
    mythread->task_active = ENDO_EMPTY;
  }

  if (mythread->task_active == ENDO_EMPTY) {  /* top level as tasks */
    ENDO_INDEX task_index = CreateTask(base);
    if (task_index == ENDO_EMPTY) {
      Warning("endoscope_begin: cannot create task item");
      return &ENDO_INVALID64;  /* only when out of tasks */
    } else {
      EndoTask *mytask = &(base->task_pool[task_index]);
      /* task_id already set in CreateTask */
      mytask->marker_id = marker_id;
      mytask->thread_index = thread_index;
      mytask->cycle_end = -1;
      mytask->log_head = ENDO_EMPTY;
      mytask->log_tail = ENDO_EMPTY;
      mytask->scope_depth = 0;
      mytask->cycle_begin = cycle_created;  /* write something before attached to thread */
      /* ASSERT mytask->next_task = ENDO_EMPTY */
      mythread->task_active = task_index;  /* attach to thread last */
      return &(mytask->cycle_begin);  /* immediately rewritten with cycle_now() after return */
    }
  } else {  /* lower lever as scope */
    EndoTask *mytask = &(base->task_pool[mythread->task_active]);
    ENDO_INDEX atom_index = CreateAtom(base);
    mytask->scope_depth++;  /* no matter success or fail */
    if (atom_index == ENDO_EMPTY) {
      Warning("endoscope_begin: cannot create atom item");
      return &ENDO_INVALID64;  /* only when out of atoms */
    } else {
      EndoAtom *myatom = &(base->atom_pool[atom_index]);
      myatom->type = 1;  /* EndoAtomPB::SCOPE_BEGIN */
      myatom->param = marker_id;
      myatom->cycle = cycle_created;  /* write something before attached to log */
      /* ASSERT myatom->next_atom = ENDO_EMPTY */
      if (mytask->log_head == ENDO_EMPTY) {
        mytask->log_head = atom_index;
      } else {
        base->atom_pool[mytask->log_tail].next_atom = atom_index;
      }
      mytask->log_tail = atom_index;
      return &(myatom->cycle);  /* immediately rewritten with cycle_now() after return */
    }
  }
}

void endoscope_end(EndoBase *base, const char *name, ENDO_INT64 cycle_end) {
  /* thread safe applied */
  int thread_id = ENDO_GETTID();
  ENDO_INDEX thread_index = GetThread(base, thread_id);
  if (thread_index == ENDO_EMPTY) {
    Warning("endoscope_end: cannot find thread item (begin-end mispair)");
    return;  /* only when thread invalid */
  } else {
    EndoThread *mythread = &(base->thread_pool[thread_index]);
    ENDO_INDEX task_index = mythread->task_active;
    if (task_index == ENDO_EMPTY) {
      Warning("endoscope_end: no active task on current thread (begin-end mispair)");
      return;  /* only when task invalid */
    } else {
      EndoTask *mytask = &(base->task_pool[task_index]);
      if (mytask->scope_depth == 0) {  /* task complete */
        if (name != NULL) {  /* check marker name match */
          if (strcmp(name, base->marker_pool[mytask->marker_id].name) != 0) {
            Warning2("endoscope_end: begin-end mispair EndMarker != BeginMarker",
                    name, base->marker_pool[mytask->marker_id].name);
          }
        }
        mytask->cycle_end = cycle_end;
        ENDO_LOCK(base->mutex);  /* task_history and task_withatom must be in the same lock */
        if (base->task_history_head == ENDO_EMPTY) {  /* attach to task_history */
          base->task_history_head = task_index;
        } else {
          base->task_pool[base->task_history_tail].next_task = task_index;
        }
        base->task_history_tail = task_index;
        /* at this intermediate state, task is both active and history to prevent red span */
        /* client can handle the duplication so no worry here */
        if (mytask->log_head != ENDO_EMPTY) {  /* this task has atoms - attach to task_withatom */
          if (base->task_withatom_head == ENDO_EMPTY) {
            base->task_withatom_head = task_index;
          } else {
            base->task_pool[base->task_withatom_tail].next_taskwithatom = task_index;
          }
          base->task_withatom_tail = task_index;
        }
        ENDO_UNLOCK(base->mutex);
        mythread->task_active = ENDO_EMPTY;  /* remove from thread's active-task */
      } else {  /* scope complete */
        ENDO_INDEX atom_index = CreateAtom(base);
        mytask->scope_depth--;  /* no matter success or fail */
        if (atom_index == ENDO_EMPTY) {
          Warning("endoscope_end: cannot create atom item");
          return;  /* only when out of atoms */
        } else {
          EndoAtom *myatom = &(base->atom_pool[atom_index]);
          myatom->cycle = cycle_end;
          myatom->type = 2;  /* EndoAtomPB::SCOPE_END */
          myatom->param = -1;
          if (mytask->log_head == ENDO_EMPTY) {
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

static void endoscope_midpoint(EndoBase *base, const char *name, const char *file, const int line,
    const char* function_name, ENDO_INT64 cycle_event, ENDO_INDEX marker_type, ENDO_INDEX atom_type) {
  ENDO_INDEX marker_id;
  EndoMarker *mymarker;
  int thread_id;
  ENDO_INDEX thread_index;
  EndoThread *mythread;

  marker_id = GetOrCreateMarker(base, line, name);
  if (marker_id == ENDO_EMPTY) {
    Warning("endoscope_midpoint: cannot create marker item");
    return;  /* only when out of markers */
  }

  mymarker = &(base->marker_pool[marker_id]);
  if (mymarker->name == NULL) {  /* created new marker if not found */
#ifdef ENDOSCOPE_COPY_MARKER_NAME
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

  thread_id = ENDO_GETTID();
  thread_index = GetThread(base, thread_id);
  if (thread_index == ENDO_EMPTY) {
    Warning2("endoscope_midpoint cannot find thread item",
             "midpoint not in scope", mymarker->name);
    return;  /* only when thread invalid */
  }
  mythread = &(base->thread_pool[thread_index]);

  if (mythread->task_active == ENDO_EMPTY) {  /* top level - invalid */
    Warning2("endoscope_midpoint no active task on current thread",
             "midpoint not in scope", mymarker->name);
    return;
  } else {  /* should be in task or scope */
    EndoTask *mytask = &(base->task_pool[mythread->task_active]);
    ENDO_INDEX atom_index = CreateAtom(base);
    if (atom_index == ENDO_EMPTY) {
      Warning("endoscope_midpoint cannot create atom item");
      return;  /* only when out of atoms */
    } else {
      EndoAtom *myatom = &(base->atom_pool[atom_index]);
      myatom->cycle = cycle_event;
      myatom->type = atom_type;
      myatom->param = marker_id;
      if (mytask->log_head == ENDO_EMPTY) {
        mytask->log_head = atom_index;
      } else {
        base->atom_pool[mytask->log_tail].next_atom = atom_index;
      }
      mytask->log_tail = atom_index;
    }
  }
}

void endoscope_event(EndoBase *base, const char *name, const char *file, const int line,
                   const char* function_name, ENDO_INT64 cycle_event) {
  endoscope_midpoint(base, name, file, line, function_name, cycle_event,
                      3 /* EndoMarkerPB::EVENT */ , 
                      5 /* EndoAtomPB::EVENT */ );
}

void endoscope_error(EndoBase *base, const char *name, const char *file, const int line,
                   const char* function_name, ENDO_INT64 cycle_event) {
  endoscope_midpoint(base, name, file, line, function_name, cycle_event,
                      4 /* EndoMarkerPB::ERROR */ , 
                      6 /* EndoAtomPB::ERROR */ );
}
