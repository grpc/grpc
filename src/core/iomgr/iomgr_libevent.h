/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPC_INTERNAL_IOMGR_IOMGR_LIBEVENT_H__
#define __GRPC_INTERNAL_IOMGR_IOMGR_LIBEVENT_H__

#include "src/core/iomgr/iomgr.h"
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

typedef struct grpc_fd grpc_fd;

/* gRPC event manager task "base class". This is pretend-inheritance in C89.
   This should be the first member of any actual grpc_em task type.

   Memory warning: expanding this will increase memory usage in any derived
   class, so be careful.

   For generality, this base can be on multiple task queues and can have
   multiple event callbacks registered. Not all "derived classes" will use
   this feature. */

typedef enum grpc_libevent_task_type {
  GRPC_EM_TASK_ALARM,
  GRPC_EM_TASK_FD,
  GRPC_EM_TASK_DO_NOT_USE
} grpc_libevent_task_type;

/* Different activity types to shape the callback and queueing arrays */
typedef enum grpc_em_task_activity_type {
  GRPC_EM_TA_READ, /* use this also for single-type events */
  GRPC_EM_TA_WRITE,
  GRPC_EM_TA_COUNT
} grpc_em_task_activity_type;

/* Include the following #define for convenience for tasks like alarms that
   only have a single type */
#define GRPC_EM_TA_ONLY GRPC_EM_TA_READ

typedef struct grpc_libevent_activation_data {
  struct event *ev;      /* event activated on this callback type */
  grpc_iomgr_cb_func cb; /* function pointer for callback */
  void *arg;             /* argument passed to cb */

  /* Hold the status associated with the callback when queued */
  grpc_iomgr_cb_status status;
  /* Now set up to link activations into scheduler queues */
  struct grpc_libevent_activation_data *prev;
  struct grpc_libevent_activation_data *next;
} grpc_libevent_activation_data;

typedef struct grpc_libevent_task {
  grpc_libevent_task_type type;

  /* Now have an array of activation data elements: one for each activity
     type that could get activated */
  grpc_libevent_activation_data activation[GRPC_EM_TA_COUNT];
} grpc_libevent_task;

/* Initialize *em_fd.
   Requires fd is a non-blocking file descriptor.

   This takes ownership of closing fd.

   Requires:  *em_fd uninitialized. fd is a non-blocking file descriptor.  */
grpc_fd *grpc_fd_create(int fd);

/* Cause *em_fd no longer to be initialized and closes the underlying fd.
   Requires: *em_fd initialized; no outstanding notify_on_read or
   notify_on_write.  */
void grpc_fd_destroy(grpc_fd *em_fd);

/* Returns the file descriptor associated with *em_fd. */
int grpc_fd_get(grpc_fd *em_fd);

/* Register read interest, causing read_cb to be called once when em_fd becomes
   readable, on deadline specified by deadline, or on shutdown triggered by
   grpc_fd_shutdown.
   read_cb will be called with read_cb_arg when *em_fd becomes readable.
   read_cb is Called with status of GRPC_CALLBACK_SUCCESS if readable,
   GRPC_CALLBACK_TIMED_OUT if the call timed out,
   and CANCELLED if the call was cancelled.

   Requires:This method must not be called before the read_cb for any previous
   call runs. Edge triggered events are used whenever they are supported by the
   underlying platform. This means that users must drain em_fd in read_cb before
   calling notify_on_read again. Users are also expected to handle spurious
   events, i.e read_cb is called while nothing can be readable from em_fd  */
int grpc_fd_notify_on_read(grpc_fd *em_fd, grpc_iomgr_cb_func read_cb,
                           void *read_cb_arg, gpr_timespec deadline);

/* Exactly the same semantics as above, except based on writable events.  */
int grpc_fd_notify_on_write(grpc_fd *fd, grpc_iomgr_cb_func write_cb,
                            void *write_cb_arg, gpr_timespec deadline);

/* Cause any current and all future read/write callbacks to error out with
   GRPC_CALLBACK_CANCELLED. */
void grpc_fd_shutdown(grpc_fd *em_fd);

/* =================== Event caching ===================
   In order to not miss or double-return edges in the context of edge triggering
   and multithreading, we need a per-fd caching layer in the eventmanager itself
   to cache relevant events.

   There are two types of events we care about: calls to notify_on_[read|write]
   and readable/writable events for the socket from eventfd. There are separate
   event caches for read and write.

   There are three states:
   0. "waiting" -- There's been a call to notify_on_[read|write] which has not
   had a corresponding event. In other words, we're waiting for an event so we
   can run the callback.
   1. "idle" -- We are neither waiting nor have a cached event.
   2. "cached" -- There has been a read/write event without a waiting callback,
   so we want to run the event next time the application calls
   notify_on_[read|write].

   The high level state diagram:

   +--------------------------------------------------------------------+
   | WAITING                  | IDLE                | CACHED            |
   |                          |                     |                   |
   |                     1. --*->              2. --+->           3.  --+\
   |                          |                     |                <--+/
   |                          |                     |                   |
  x+-- 6.                5. <-+--              4. <-*--                 |
   |                          |                     |                   |
   +--------------------------------------------------------------------+

   Transitions right occur on read|write events. Transitions left occur on
   notify_on_[read|write] events.
   State transitions:
   1. Read|Write event while waiting -> run the callback and transition to idle.
   2. Read|Write event while idle -> transition to cached.
   3. Read|Write event with one already cached -> still cached.
   4. notify_on_[read|write] with event cached: run callback and transition to
      idle.
   5. notify_on_[read|write] when idle: Store callback and transition to
      waiting.
   6. notify_on_[read|write] when waiting: invalid. */

typedef enum grpc_fd_state {
  GRPC_FD_WAITING = 0,
  GRPC_FD_IDLE = 1,
  GRPC_FD_CACHED = 2
} grpc_fd_state;

/* gRPC file descriptor handle.
   The handle is used to register read/write callbacks to a file descriptor */
struct grpc_fd {
  grpc_libevent_task task; /* Base class, callbacks, queues, etc */
  int fd;                  /* File descriptor */

  /* Note that the shutdown event is only needed as a workaround for libevent
     not properly handling event_active on an in flight event. */
  struct event *shutdown_ev; /* activated to trigger shutdown */

  /* protect shutdown_started|read_state|write_state and ensure barriers
     between notify_on_[read|write] and read|write callbacks */
  gpr_mu mu;
  int shutdown_started; /* 0 -> shutdown not started, 1 -> started */
  grpc_fd_state read_state;
  grpc_fd_state write_state;

  /* descriptor delete list. These are destroyed during polling. */
  struct grpc_fd *next;
};

/* gRPC alarm handle.
   The handle is used to add an alarm which expires after specified timeout. */
struct grpc_alarm {
  grpc_libevent_task task; /* Include the base class */

  gpr_atm triggered; /* To be used atomically if alarm triggered */
};

#endif /* __GRPC_INTERNAL_IOMGR_IOMGR_LIBEVENT_H__ */
