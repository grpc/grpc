#include <stdio.h>

#include "src/cpp/profiling/endoscope_frontend.h"

namespace perftools {
namespace endoscope {

/* Protobuf Output */

static void WriteAtom(EndoBase *base, EndoAtomPB *atom, EndoAtom *myatom,
               ENDO_INT64 cycle_last, ENDO_INT64 timestamp) {
  atom->set_cycle(myatom->cycle - cycle_last);
  atom->set_type((EndoAtomPB_AtomType)(myatom->type));
  if (myatom->type == 1 /* SCOPE_BEGIN */ ||
      myatom->type == 5 /* EVENT */ ||
      myatom->type == 6 /* ERROR */ ) {
    atom->set_param((EndoAtomPB_AtomType)(myatom->param));
    base->marker_pool[myatom->param].timestamp = timestamp;
  }
}

static void WriteTask(EndoBase *base, EndoTaskPB *task, EndoTask *mytask, ENDO_INT64 timestamp) {
  task->set_task_id(mytask->task_id);
  task->set_parent_id(-1);
  task->set_marker_id(mytask->marker_id);
  task->set_thread_id(base->thread_pool[mytask->thread_index].thread_id);
  task->set_cycle_begin(mytask->cycle_begin);
  task->set_cycle_end(mytask->cycle_end);
  base->marker_pool[mytask->marker_id].timestamp = timestamp;
  base->thread_pool[mytask->thread_index].timestamp = timestamp;

  ENDO_INT64 cycle_last = mytask->cycle_begin;
  ENDO_INDEX atom_index;
  for (atom_index = mytask->log_head;
       atom_index != ENDO_EMPTY;
       atom_index = base->atom_pool[atom_index].next_atom) {
    EndoAtom *myatom = &(base->atom_pool[atom_index]);
    if (myatom->type == 0) {
      break;  /* INVALID */
    }
    EndoAtomPB *atom = task->add_log();
    WriteAtom(base, atom, myatom, cycle_last, timestamp);
    cycle_last = myatom->cycle;
  }
}

static void WriteMarker(EndoBase *base, EndoMarkerPB *marker, EndoMarker *mymarker, ENDO_INDEX marker_id) {
  marker->set_name(mymarker->name);
  marker->set_type((EndoMarkerPB_MarkerType)(mymarker->type));
  marker->set_marker_id(marker_id);
  marker->set_file(mymarker->file);
  marker->set_line(mymarker->line);
  marker->set_function_name(mymarker->function_name);
  marker->set_cycle_created(mymarker->cycle_created);
}

static void WriteThread(EndoBase *base, EndoThreadPB *thread, EndoThread *mythread) {
  char thread_name[20];
  sprintf(thread_name, "Thread_%d", mythread->thread_id);
  thread->set_thread_id(mythread->thread_id);
  thread->set_name(thread_name);
  thread->set_cycle_created(mythread->cycle_created);
  thread->set_cycle_terminated(-1);
}

static void WriteSync(EndoBase *base, EndoSyncPB *sync) {
  ENDO_SYNCCLOCK(&(base->cycle_sync), &(base->time_sync));
  sync->set_cycle_begin(base->cycle_begin);
  sync->set_cycle_sync(base->cycle_sync);
  sync->set_time_begin(base->time_begin);
  sync->set_time_sync(base->time_sync);
}

void WriteSnapshot(EndoBase *base, EndoSnapshotPB* snapshot,
                   ENDO_INT64 url_cycle_begin, ENDO_INT64 url_cycle_end) {
  ENDO_INT64 timestamp = ENDO_CYCLENOW();
  ENDO_INDEX task_index, safecount, actualcount, i;

  if (url_cycle_begin == 0) url_cycle_begin = base->cycle_begin;
  if (url_cycle_end == 0) url_cycle_end = timestamp;

  for (task_index = base->task_history_head, safecount = 0, actualcount = 0;
       task_index != ENDO_EMPTY && safecount < task_capacity + 99;
       task_index = base->task_pool[task_index].next_task, safecount++) {  // task_history first
    EndoTask *mytask = &(base->task_pool[task_index]);
    if (mytask->cycle_end < url_cycle_begin) continue;
    if (mytask->cycle_begin > url_cycle_end) break;  // tasks queued with cycle_begin ascending
    EndoTaskPB *task = snapshot->add_tasks_history();
    WriteTask(base, task, mytask, timestamp);
    actualcount++;
  }
  for (i = 0, actualcount = 0; i < base->thread_count; i++) {
    EndoThread *mythread = &(base->thread_pool[i]);
    if (mythread->cycle_created == 0) continue;  // thread not fully ready
    if (mythread->task_active != ENDO_EMPTY) {  // task_active (attached to threads) second
      EndoTask *mytask = &(base->task_pool[mythread->task_active]);
      if (mytask->cycle_begin > url_cycle_end) continue;
      EndoTaskPB *task = snapshot->add_tasks_active();
      WriteTask(base, task, mytask, timestamp);
      actualcount++;
    }
  }
  if (base->marker_error->name != NULL) {  // task_error
    EndoTaskPB *task = snapshot->add_tasks_active();
    WriteTask(base, task, base->task_error, timestamp);
  }

  for (i = 0, actualcount = 0; i < base->marker_count; i++) {  // marker
    EndoMarker *mymarker = &(base->marker_pool[i]);
    if (mymarker->timestamp < timestamp) continue;  // unused or not ready
    EndoMarkerPB *marker = snapshot->add_marker();
    WriteMarker(base, marker, mymarker, i);
    actualcount++;
  }
  if (base->marker_error->name != NULL) {  // marker_error
    EndoMarkerPB *marker = snapshot->add_marker();
    WriteMarker(base, marker, base->marker_error, marker_capacity);
  }

  for (i = 0, actualcount = 0; i < base->thread_count; i++) {  // thread
    EndoThread *mythread = &(base->thread_pool[i]);
    if (mythread->timestamp < timestamp) continue;  // unused or not ready
    EndoThreadPB *thread = snapshot->add_thread();
    WriteThread(base, thread, mythread);
    actualcount++;
  }
  if (base->marker_error->name != NULL) {  // thread_error
    EndoThreadPB *thread = snapshot->add_thread();
    WriteThread(base, thread, base->thread_error);
  }

  snapshot->set_cycle_begin(url_cycle_begin);
  snapshot->set_cycle_end(url_cycle_end); 

  EndoSyncPB *sync = new EndoSyncPB();  // sync
  WriteSync(base, sync);
  snapshot->set_allocated_sync(sync);

  snapshot->set_timestamp(timestamp);
}

}  // namespace endoscope
}  // namespace perftools
