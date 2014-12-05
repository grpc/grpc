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

#ifndef __GRPC_INTERNAL_EVENTMANAGER_EM_H__
#define __GRPC_INTERNAL_EVENTMANAGER_EM_H__
/* grpc_em is an event manager wrapping event loop with multithread support.
   It executes a callback function when a specific event occurs on a file
   descriptor or after a timeout has passed.
   All methods are threadsafe and can be called from any thread.

   To use the event manager, a grpc_em instance needs to be initialized to
   maintains the internal states. The grpc_em instance can be used to
   initialize file descriptor instance of grpc_em_fd, or alarm instance of
   grpc_em_alarm. The former is used to register a callback with a IO event.
   The later is used to schedule an alarm.

   Instantiating any of these data structures requires including em_internal.h
   A typical usage example is shown in the end of that header file.  */

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

/* =============== Enums used in GRPC event manager API ==================== */

/* Result of a grpc_em operation */
typedef enum grpc_em_error {
  GRPC_EM_OK = 0,           /* everything went ok */
  GRPC_EM_ERROR,            /* internal errors not caused by the caller */
  GRPC_EM_INVALID_ARGUMENTS /* invalid arguments from the caller */
} grpc_em_error;

/* Status passed to callbacks for grpc_em_fd_notify_on_read and
   grpc_em_fd_notify_on_write.  */
typedef enum grpc_em_cb_status {
  GRPC_CALLBACK_SUCCESS = 0,
  GRPC_CALLBACK_TIMED_OUT,
  GRPC_CALLBACK_CANCELLED,
  GRPC_CALLBACK_DO_NOT_USE
} grpc_em_cb_status;

/* ======= Useful forward struct typedefs for GRPC event manager API ======= */

struct grpc_em;
struct grpc_em_alarm;
struct grpc_fd;

typedef struct grpc_em grpc_em;
typedef struct grpc_em_alarm grpc_em_alarm;
typedef struct grpc_em_fd grpc_em_fd;

/* gRPC Callback definition */
typedef void (*grpc_em_cb_func)(void *arg, grpc_em_cb_status status);

/* ============================ grpc_em =============================== */
/* Initialize *em and start polling, return GRPC_EM_OK on success, return
   GRPC_EM_ERROR on failure. Upon failure, caller should call grpc_em_destroy()
   to clean partially initialized *em.

   Requires:  *em uninitialized.  */
grpc_em_error grpc_em_init(grpc_em *em);

/* Stop polling and cause *em no longer to be initialized.
   Return GRPC_EM_OK if event polling is cleanly stopped.
   Otherwise, return GRPC_EM_ERROR if polling is shutdown with errors.
   Requires: *em initialized; no other concurrent operation on *em.  */
grpc_em_error grpc_em_destroy(grpc_em *em);

/* do some work; assumes em->mu locked; may unlock and relock em->mu */
int grpc_em_work(grpc_em *em, gpr_timespec deadline);

/* =========================== grpc_em_am ============================== */
/* Initialize *alarm. When expired or canceled, alarm_cb will be called with
   *alarm_cb_arg and status to indicate if it expired (SUCCESS) or was
   canceled (CANCELLED). alarm_cb is guaranteed to be called exactly once,
   and application code should check the status to determine how it was
   invoked. The application callback is also responsible for maintaining
   information about when to free up any user-level state.  */
grpc_em_error grpc_em_alarm_init(grpc_em_alarm *alarm, grpc_em *em,
                                 grpc_em_cb_func alarm_cb, void *alarm_cb_arg);

/* Note that there is no alarm destroy function. This is because the
   alarm is a one-time occurrence with a guarantee that the callback will
   be called exactly once, either at expiration or cancellation. Thus, all
   the internal alarm event management state is destroyed just before
   that callback is invoked. If the user has additional state associated with
   the alarm, the user is responsible for determining when it is safe to
   destroy that state. */

/* Schedule *alarm to expire at deadline. If *alarm is
   re-added before expiration, the *delay is simply reset to the new value.
   Return GRPC_EM_OK on success, or GRPC_EM_ERROR on failure.
   Upon failure, caller should abort further operations on *alarm */
grpc_em_error grpc_em_alarm_add(grpc_em_alarm *alarm, gpr_timespec deadline);

/* Cancel an *alarm.
   There are three cases:
   1. We normally cancel the alarm
   2. The alarm has already run
   3. We can't cancel the alarm because it is "in flight".

   In all of these cases, the cancellation is still considered successful.
   They are essentially distinguished in that the alarm_cb will be run
   exactly once from either the cancellation (with status CANCELLED)
   or from the activation (with status SUCCESS)

   Requires:  cancel() must happen after add() on a given alarm */
grpc_em_error grpc_em_alarm_cancel(grpc_em_alarm *alarm);

/* ========================== grpc_em_fd ============================= */

/* Initialize *em_fd, return GRPM_EM_OK on success, GRPC_EM_ERROR on internal
   errors, or GRPC_EM_INVALID_ARGUMENTS if fd is a blocking file descriptor.
   Upon failure, caller should call grpc_em_fd_destroy() to clean partially
   initialized *em_fd.
   fd is a non-blocking file descriptor.

   This takes ownership of closing fd.

   Requires:  *em_fd uninitialized. fd is a non-blocking file descriptor.  */
grpc_em_error grpc_em_fd_init(grpc_em_fd *em_fd, grpc_em *em, int fd);

/* Cause *em_fd no longer to be initialized and closes the underlying fd.
   Requires: *em_fd initialized; no outstanding notify_on_read or
   notify_on_write.  */
void grpc_em_fd_destroy(grpc_em_fd *em_fd);

/* Returns the file descriptor associated with *em_fd. */
int grpc_em_fd_get(grpc_em_fd *em_fd);

/* Returns the event manager associated with *em_fd. */
grpc_em *grpc_em_fd_get_em(grpc_em_fd *em_fd);

/* Register read interest, causing read_cb to be called once when em_fd becomes
   readable, on deadline specified by deadline, or on shutdown triggered by
   grpc_em_fd_shutdown.
   Return GRPC_EM_OK on success, or GRPC_EM_ERROR on failure.
   Upon Failure, caller should abort further operations on *em_fd except
   grpc_em_fd_shutdown().
   read_cb will be called with read_cb_arg when *em_fd becomes readable.
   read_cb is Called with status of GRPC_CALLBACK_SUCCESS if readable,
   GRPC_CALLBACK_TIMED_OUT if the call timed out,
   and CANCELLED if the call was cancelled.

   Requires:This method must not be called before the read_cb for any previous
   call runs. Edge triggered events are used whenever they are supported by the
   underlying platform. This means that users must drain em_fd in read_cb before
   calling notify_on_read again. Users are also expected to handle spurious
   events, i.e read_cb is called while nothing can be readable from em_fd  */
grpc_em_error grpc_em_fd_notify_on_read(grpc_em_fd *em_fd,
                                        grpc_em_cb_func read_cb,
                                        void *read_cb_arg,
                                        gpr_timespec deadline);

/* Exactly the same semantics as above, except based on writable events.  */
grpc_em_error grpc_em_fd_notify_on_write(grpc_em_fd *fd,
                                         grpc_em_cb_func write_cb,
                                         void *write_cb_arg,
                                         gpr_timespec deadline);

/* Cause any current and all future read/write callbacks to error out with
   GRPC_CALLBACK_CANCELLED. */
void grpc_em_fd_shutdown(grpc_em_fd *em_fd);

/* ================== Other functions =================== */

/* This function is called from within a callback or from anywhere else
   and causes the invocation of a callback at some point in the future */
grpc_em_error grpc_em_add_callback(grpc_em *em, grpc_em_cb_func cb,
                                   void *cb_arg);

/* ========== Declarations related to queue management (non-API) =========== */

/* Forward declarations */
struct grpc_em_activation_data;

/* ================== Actual structure definitions ========================= */
/* gRPC event manager handle.
   The handle is used to initialize both grpc_em_alarm and grpc_em_fd. */
struct em_thread_arg;

struct grpc_em {
  struct event_base *event_base;

  gpr_mu mu;
  gpr_cv cv;
  struct grpc_em_activation_data *q;
  int num_pollers;
  int num_fds;
  gpr_timespec last_poll_completed;

  int shutdown_backup_poller;
  gpr_event backup_poller_done;

  struct event *timeout_ev; /* activated to break out of the event loop early */
};

/* gRPC event manager task "base class". This is pretend-inheritance in C89.
   This should be the first member of any actual grpc_em task type.

   Memory warning: expanding this will increase memory usage in any derived
   class, so be careful.

   For generality, this base can be on multiple task queues and can have
   multiple event callbacks registered. Not all "derived classes" will use
   this feature. */

typedef enum grpc_em_task_type {
  GRPC_EM_TASK_ALARM,
  GRPC_EM_TASK_FD,
  GRPC_EM_TASK_DO_NOT_USE
} grpc_em_task_type;

/* Different activity types to shape the callback and queueing arrays */
typedef enum grpc_em_task_activity_type {
  GRPC_EM_TA_READ, /* use this also for single-type events */
  GRPC_EM_TA_WRITE,
  GRPC_EM_TA_COUNT
} grpc_em_task_activity_type;

/* Include the following #define for convenience for tasks like alarms that
   only have a single type */
#define GRPC_EM_TA_ONLY GRPC_EM_TA_READ

typedef struct grpc_em_activation_data {
  struct event *ev;   /* event activated on this callback type */
  grpc_em_cb_func cb; /* function pointer for callback */
  void *arg;          /* argument passed to cb */

  /* Hold the status associated with the callback when queued */
  grpc_em_cb_status status;
  /* Now set up to link activations into scheduler queues */
  struct grpc_em_activation_data *prev;
  struct grpc_em_activation_data *next;
} grpc_em_activation_data;

typedef struct grpc_em_task {
  grpc_em_task_type type;
  grpc_em *em;

  /* Now have an array of activation data elements: one for each activity
     type that could get activated */
  grpc_em_activation_data activation[GRPC_EM_TA_COUNT];
} grpc_em_task;

/* gRPC alarm handle.
   The handle is used to add an alarm which expires after specified timeout. */
struct grpc_em_alarm {
  grpc_em_task task; /* Include the base class */

  gpr_atm triggered; /* To be used atomically if alarm triggered */
};

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

typedef enum grpc_em_fd_state {
  GRPC_EM_FD_WAITING = 0,
  GRPC_EM_FD_IDLE = 1,
  GRPC_EM_FD_CACHED = 2
} grpc_em_fd_state;

/* gRPC file descriptor handle.
   The handle is used to register read/write callbacks to a file descriptor */
struct grpc_em_fd {
  grpc_em_task task; /* Base class, callbacks, queues, etc */
  int fd;            /* File descriptor */

  /* Note that the shutdown event is only needed as a workaround for libevent
     not properly handling event_active on an in flight event. */
  struct event *shutdown_ev; /* activated to trigger shutdown */

  /* protect shutdown_started|read_state|write_state and ensure barriers
     between notify_on_[read|write] and read|write callbacks */
  gpr_mu mu;
  int shutdown_started; /* 0 -> shutdown not started, 1 -> started */
  grpc_em_fd_state read_state;
  grpc_em_fd_state write_state;
  /* activated after some timeout to activate shutdown_ev */
};

#endif  /* __GRPC_INTERNAL_EVENTMANAGER_EM_H__ */
