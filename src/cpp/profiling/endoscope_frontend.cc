/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/cpp/profiling/endoscope_frontend.h"

#ifdef GRPC_ENDOSCOPE_PROFILER

#include <stdio.h>

namespace perftools {
namespace endoscope {

/* Protobuf Output */

static void WriteAtom(grpc_endo_base *base, EndoAtomPB *atom, grpc_endo_atom *myatom,
               gpr_int64 cycle_last, gpr_int64 timestamp) {
  atom->set_cycle(myatom->cycle - cycle_last);
  atom->set_type((EndoAtomPB_AtomType)(myatom->type));
  if (myatom->type == 1 /* SCOPE_BEGIN */ ||
      myatom->type == 5 /* EVENT */ ||
      myatom->type == 6 /* ERROR */ ) {
    atom->set_param((EndoAtomPB_AtomType)(myatom->param));
    base->marker_pool[myatom->param].timestamp = timestamp;
  }
}

static void WriteTask(grpc_endo_base *base, EndoTaskPB *task, grpc_endo_task *mytask, gpr_int64 timestamp) {
  task->set_task_id(mytask->task_id);
  task->set_parent_id(-1);
  task->set_marker_id(mytask->marker_id);
  task->set_thread_id(base->thread_pool[mytask->thread_index].thread_id);
  task->set_cycle_begin(mytask->cycle_begin);
  task->set_cycle_end(mytask->cycle_end);
  base->marker_pool[mytask->marker_id].timestamp = timestamp;
  base->thread_pool[mytask->thread_index].timestamp = timestamp;

  gpr_int64 cycle_last = mytask->cycle_begin;
  GRPC_ENDO_INDEX atom_index;
  for (atom_index = mytask->log_head;
       atom_index != GRPC_ENDO_EMPTY;
       atom_index = base->atom_pool[atom_index].next_atom) {
    grpc_endo_atom *myatom = &(base->atom_pool[atom_index]);
    if (myatom->type == 0) {
      break;  /* INVALID */
    }
    EndoAtomPB *atom = task->add_log();
    WriteAtom(base, atom, myatom, cycle_last, timestamp);
    cycle_last = myatom->cycle;
  }
}

static void WriteMarker(grpc_endo_base *base, EndoMarkerPB *marker, grpc_endo_marker *mymarker, GRPC_ENDO_INDEX marker_id) {
  marker->set_name(mymarker->name);
  marker->set_type((EndoMarkerPB_MarkerType)(mymarker->type));
  marker->set_marker_id(marker_id);
  marker->set_file(mymarker->file);
  marker->set_line(mymarker->line);
  marker->set_function_name(mymarker->function_name);
  marker->set_cycle_created(mymarker->cycle_created);
}

static void WriteThread(grpc_endo_base *base, EndoThreadPB *thread, grpc_endo_thread *mythread) {
  char thread_name[20];
  sprintf(thread_name, "%lu", mythread->thread_id);
  thread->set_thread_id(mythread->thread_id);
  thread->set_name(thread_name);
  thread->set_cycle_created(mythread->cycle_created);
  thread->set_cycle_terminated(-1);
}

static void WriteSync(grpc_endo_base *base, EndoSyncPB *sync) {
  grpc_endo_syncclock(&(base->cycle_sync), &(base->time_sync));
  sync->set_cycle_begin(base->cycle_begin);
  sync->set_cycle_sync(base->cycle_sync);
  sync->set_time_begin(base->time_begin);
  sync->set_time_sync(base->time_sync);
}

void WriteSnapshot(grpc_endo_base *base, EndoSnapshotPB* snapshot,
                   gpr_int64 url_cycle_begin, gpr_int64 url_cycle_end) {
  gpr_int64 timestamp = grpc_endo_cyclenow();
  GRPC_ENDO_INDEX task_index, safecount, actualcount, i;

  if (url_cycle_begin == 0) url_cycle_begin = base->cycle_begin;
  if (url_cycle_end == 0) url_cycle_end = timestamp;

  for (task_index = base->task_history_head, safecount = 0, actualcount = 0;
       task_index != GRPC_ENDO_EMPTY && safecount < GRPC_ENDO_TASK_CAPACITY;
       task_index = base->task_pool[task_index].next_task, safecount++) {  // task_history first
    grpc_endo_task *mytask = &(base->task_pool[task_index]);
    if (mytask->cycle_end < url_cycle_begin) continue;
    if (mytask->cycle_begin > url_cycle_end) break;  // tasks queued with cycle_begin ascending
    EndoTaskPB *task = snapshot->add_tasks_history();
    WriteTask(base, task, mytask, timestamp);
    actualcount++;
  }
  for (i = 0, actualcount = 0; i < base->thread_count; i++) {
    grpc_endo_thread *mythread = &(base->thread_pool[i]);
    if (mythread->cycle_created == 0) continue;  // thread not fully ready
    if (mythread->task_active != GRPC_ENDO_EMPTY) {  // task_active (attached to threads) second
      grpc_endo_task *mytask = &(base->task_pool[mythread->task_active]);
      if (mytask->cycle_begin > url_cycle_end) continue;
      EndoTaskPB *task = snapshot->add_tasks_active();
      WriteTask(base, task, mytask, timestamp);
      actualcount++;
    }
  }
  if (base->marker_warning->name != NULL) {  // task_warning
    EndoTaskPB *task = snapshot->add_tasks_active();
    WriteTask(base, task, base->task_warning, timestamp);
  }

  for (i = 0, actualcount = 0; i < base->marker_count; i++) {  // marker
    grpc_endo_marker *mymarker = &(base->marker_pool[i]);
    if (mymarker->timestamp < timestamp) continue;  // unused or not ready
    EndoMarkerPB *marker = snapshot->add_marker();
    WriteMarker(base, marker, mymarker, i);
    actualcount++;
  }
  if (base->marker_warning->name != NULL) {  // marker_warning
    EndoMarkerPB *marker = snapshot->add_marker();
    WriteMarker(base, marker, base->marker_warning, GRPC_ENDO_MARKER_CAPACITY);
  }

  for (i = 0, actualcount = 0; i < base->thread_count; i++) {  // thread
    grpc_endo_thread *mythread = &(base->thread_pool[i]);
    if (mythread->timestamp < timestamp) continue;  // unused or not ready
    EndoThreadPB *thread = snapshot->add_thread();
    WriteThread(base, thread, mythread);
    actualcount++;
  }
  if (base->marker_warning->name != NULL) {  // thread_warning
    EndoThreadPB *thread = snapshot->add_thread();
    WriteThread(base, thread, base->thread_warning);
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

#endif  // GRPC_ENDOSCOPE_PROFILER
