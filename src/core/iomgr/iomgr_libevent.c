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

#include "src/core/iomgr/iomgr_libevent.h"

#include <unistd.h>
#include <fcntl.h>

#include "src/core/iomgr/alarm.h"
#include <grpc/support/atm.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <event2/event.h>
#include <event2/thread.h>

#define ALARM_TRIGGER_INIT ((gpr_atm)0)
#define ALARM_TRIGGER_INCREMENT ((gpr_atm)1)
#define DONE_SHUTDOWN ((void *)1)

#define POLLER_ID_INVALID ((gpr_atm)-1)

/* Global data */
struct event_base *g_event_base;
gpr_mu grpc_iomgr_mu;
gpr_cv grpc_iomgr_cv;
static grpc_libevent_activation_data *g_activation_queue;
static int g_num_pollers;
static int g_num_fds;
static int g_num_address_resolutions;
static gpr_timespec g_last_poll_completed;
static int g_shutdown_backup_poller;
static gpr_event g_backup_poller_done;
/* activated to break out of the event loop early */
static struct event *g_timeout_ev;
static grpc_fd *g_fds_to_free;

int evthread_use_threads(void);
static void grpc_fd_impl_destroy(grpc_fd *impl);

void grpc_iomgr_ref_address_resolution(int delta) {
  gpr_mu_lock(&grpc_iomgr_mu);
  gpr_log(GPR_DEBUG, "num_address_resolutions = %d + %d",
          g_num_address_resolutions, delta);
  GPR_ASSERT(!g_shutdown_backup_poller);
  g_num_address_resolutions += delta;
  if (0 == g_num_address_resolutions) {
    gpr_cv_broadcast(&grpc_iomgr_cv);
  }
  gpr_mu_unlock(&grpc_iomgr_mu);
}

/* If anything is in the work queue, process one item and return 1.
   Return 0 if there were no work items to complete.
   Requires grpc_iomgr_mu locked, may unlock and relock during the call. */
static int maybe_do_queue_work() {
  grpc_libevent_activation_data *work = g_activation_queue;

  if (work == NULL) return 0;

  if (work->next == work) {
    g_activation_queue = NULL;
  } else {
    g_activation_queue = work->next;
    g_activation_queue->prev = work->prev;
    g_activation_queue->next->prev = g_activation_queue->prev->next =
        g_activation_queue;
  }
  work->next = work->prev = NULL;
  /* force status to cancelled from ok when shutting down */
  if (g_shutdown_backup_poller && work->status == GRPC_CALLBACK_SUCCESS) {
    work->status = GRPC_CALLBACK_CANCELLED;
  }
  gpr_mu_unlock(&grpc_iomgr_mu);

  work->cb(work->arg, work->status);

  gpr_mu_lock(&grpc_iomgr_mu);
  return 1;
}

/* Break out of the event loop on timeout */
static void timer_callback(int fd, short events, void *context) {
  event_base_loopbreak((struct event_base *)context);
}

static void free_fd_list(grpc_fd *impl) {
  while (impl != NULL) {
    grpc_fd *current = impl;
    impl = impl->next;
    grpc_fd_impl_destroy(current);
    gpr_free(current);
  }
}

static void maybe_free_fds() {
  if (g_fds_to_free) {
    free_fd_list(g_fds_to_free);
    g_fds_to_free = NULL;
  }
}

/* Spend some time doing polling and libevent maintenance work if no other
   thread is. This includes both polling for events and destroying/closing file
   descriptor objects.
   Returns 1 if polling was performed, 0 otherwise.
   Requires grpc_iomgr_mu locked, may unlock and relock during the call. */
static int maybe_do_polling_work(struct timeval delay) {
  int status;

  if (g_num_pollers) return 0;

  g_num_pollers = 1;

  maybe_free_fds();

  gpr_mu_unlock(&grpc_iomgr_mu);

  event_add(g_timeout_ev, &delay);
  status = event_base_loop(g_event_base, EVLOOP_ONCE);
  if (status < 0) {
    gpr_log(GPR_ERROR, "event polling loop stops with error status %d", status);
  }
  event_del(g_timeout_ev);

  gpr_mu_lock(&grpc_iomgr_mu);
  maybe_free_fds();

  g_num_pollers = 0;
  gpr_cv_broadcast(&grpc_iomgr_cv);
  return 1;
}

int grpc_iomgr_work(gpr_timespec deadline) {
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  /* poll for no longer than one second */
  gpr_timespec max_delay = {1, 0};
  struct timeval delay;

  if (gpr_time_cmp(delay_timespec, gpr_time_0) <= 0) {
    return 0;
  }

  if (gpr_time_cmp(delay_timespec, max_delay) > 0) {
    delay_timespec = max_delay;
  }

  delay = gpr_timeval_from_timespec(delay_timespec);

  if (maybe_do_queue_work() || maybe_do_polling_work(delay)) {
    g_last_poll_completed = gpr_now();
    return 1;
  }

  return 0;
}

static void backup_poller_thread(void *p) {
  int backup_poller_engaged = 0;
  /* allow no pollers for 100 milliseconds, then engage backup polling */
  gpr_timespec allow_no_pollers = gpr_time_from_micros(100 * 1000);

  gpr_mu_lock(&grpc_iomgr_mu);
  while (!g_shutdown_backup_poller) {
    if (g_num_pollers == 0) {
      gpr_timespec now = gpr_now();
      gpr_timespec time_until_engage = gpr_time_sub(
          allow_no_pollers, gpr_time_sub(now, g_last_poll_completed));
      if (gpr_time_cmp(time_until_engage, gpr_time_0) <= 0) {
        if (!backup_poller_engaged) {
          gpr_log(GPR_DEBUG, "No pollers for a while - engaging backup poller");
          backup_poller_engaged = 1;
        }
        if (!maybe_do_queue_work()) {
          struct timeval tv = {1, 0};
          maybe_do_polling_work(tv);
        }
      } else {
        if (backup_poller_engaged) {
          gpr_log(GPR_DEBUG, "Backup poller disengaged");
          backup_poller_engaged = 0;
        }
        gpr_mu_unlock(&grpc_iomgr_mu);
        gpr_sleep_until(gpr_time_add(now, time_until_engage));
        gpr_mu_lock(&grpc_iomgr_mu);
      }
    } else {
      if (backup_poller_engaged) {
        gpr_log(GPR_DEBUG, "Backup poller disengaged");
        backup_poller_engaged = 0;
      }
      gpr_cv_wait(&grpc_iomgr_cv, &grpc_iomgr_mu, gpr_inf_future);
    }
  }
  gpr_mu_unlock(&grpc_iomgr_mu);

  gpr_event_set(&g_backup_poller_done, (void *)1);
}

void grpc_iomgr_init() {
  gpr_thd_id backup_poller_id;

  if (evthread_use_threads() != 0) {
    gpr_log(GPR_ERROR, "Failed to initialize libevent thread support!");
    abort();
  }

  gpr_mu_init(&grpc_iomgr_mu);
  gpr_cv_init(&grpc_iomgr_cv);
  g_activation_queue = NULL;
  g_num_pollers = 0;
  g_num_fds = 0;
  g_num_address_resolutions = 0;
  g_last_poll_completed = gpr_now();
  g_shutdown_backup_poller = 0;
  g_fds_to_free = NULL;

  gpr_event_init(&g_backup_poller_done);

  g_event_base = NULL;
  g_timeout_ev = NULL;

  g_event_base = event_base_new();
  if (!g_event_base) {
    gpr_log(GPR_ERROR, "Failed to create the event base");
    abort();
  }

  if (evthread_make_base_notifiable(g_event_base) != 0) {
    gpr_log(GPR_ERROR, "Couldn't make event base notifiable cross threads!");
    abort();
  }

  g_timeout_ev = evtimer_new(g_event_base, timer_callback, g_event_base);

  gpr_thd_new(&backup_poller_id, backup_poller_thread, NULL, NULL);
}

void grpc_iomgr_shutdown() {
  gpr_timespec fd_shutdown_deadline =
      gpr_time_add(gpr_now(), gpr_time_from_seconds(10));

  /* broadcast shutdown */
  gpr_mu_lock(&grpc_iomgr_mu);
  while (g_num_fds > 0 || g_num_address_resolutions > 0) {
    gpr_log(GPR_INFO,
            "waiting for %d fds and %d name resolutions to be destroyed before "
            "closing event manager",
            g_num_fds, g_num_address_resolutions);
    if (gpr_cv_wait(&grpc_iomgr_cv, &grpc_iomgr_mu, fd_shutdown_deadline)) {
      gpr_log(GPR_ERROR,
              "not all fds or name resolutions destroyed before shutdown "
              "deadline: memory leaks "
              "are likely");
      break;
    } else if (g_num_fds == 0 && g_num_address_resolutions == 0) {
      gpr_log(GPR_INFO, "all fds closed, all name resolutions finished");
    }
  }

  g_shutdown_backup_poller = 1;
  gpr_cv_broadcast(&grpc_iomgr_cv);
  gpr_mu_unlock(&grpc_iomgr_mu);

  gpr_event_wait(&g_backup_poller_done, gpr_inf_future);

  /* drain pending work */
  gpr_mu_lock(&grpc_iomgr_mu);
  while (maybe_do_queue_work())
    ;
  gpr_mu_unlock(&grpc_iomgr_mu);

  free_fd_list(g_fds_to_free);

  /* complete shutdown */
  gpr_mu_destroy(&grpc_iomgr_mu);
  gpr_cv_destroy(&grpc_iomgr_cv);

  if (g_timeout_ev != NULL) {
    event_free(g_timeout_ev);
  }

  if (g_event_base != NULL) {
    event_base_free(g_event_base);
    g_event_base = NULL;
  }
}

static void add_task(grpc_libevent_activation_data *adata) {
  gpr_mu_lock(&grpc_iomgr_mu);
  if (g_activation_queue) {
    adata->next = g_activation_queue;
    adata->prev = adata->next->prev;
    adata->next->prev = adata->prev->next = adata;
  } else {
    g_activation_queue = adata;
    adata->next = adata->prev = adata;
  }
  gpr_cv_broadcast(&grpc_iomgr_cv);
  gpr_mu_unlock(&grpc_iomgr_mu);
}

/* ===============grpc_alarm implementation==================== */

/* The following function frees up the alarm's libevent structure and
   should always be invoked just before calling the alarm's callback */
static void alarm_ev_destroy(grpc_alarm *alarm) {
  grpc_libevent_activation_data *adata =
      &alarm->task.activation[GRPC_EM_TA_ONLY];
  if (adata->ev != NULL) {
    /* TODO(klempner): Is this safe to do when we're cancelling? */
    event_free(adata->ev);
    adata->ev = NULL;
  }
}
/* Proxy callback triggered by alarm->ev to call alarm->cb */
static void libevent_alarm_cb(int fd, short what, void *arg /*=alarm*/) {
  grpc_alarm *alarm = arg;
  grpc_libevent_activation_data *adata =
      &alarm->task.activation[GRPC_EM_TA_ONLY];
  int trigger_old;

  /* First check if this alarm has been canceled, atomically */
  trigger_old =
      gpr_atm_full_fetch_add(&alarm->triggered, ALARM_TRIGGER_INCREMENT);
  if (trigger_old == ALARM_TRIGGER_INIT) {
    /* Before invoking user callback, destroy the libevent structure */
    alarm_ev_destroy(alarm);
    adata->status = GRPC_CALLBACK_SUCCESS;
    add_task(adata);
  }
}

void grpc_alarm_init(grpc_alarm *alarm, grpc_iomgr_cb_func alarm_cb,
                     void *alarm_cb_arg) {
  grpc_libevent_activation_data *adata =
      &alarm->task.activation[GRPC_EM_TA_ONLY];
  alarm->task.type = GRPC_EM_TASK_ALARM;
  gpr_atm_rel_store(&alarm->triggered, ALARM_TRIGGER_INIT);
  adata->cb = alarm_cb;
  adata->arg = alarm_cb_arg;
  adata->prev = NULL;
  adata->next = NULL;
  adata->ev = NULL;
}

int grpc_alarm_add(grpc_alarm *alarm, gpr_timespec deadline) {
  grpc_libevent_activation_data *adata =
      &alarm->task.activation[GRPC_EM_TA_ONLY];
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  struct timeval delay = gpr_timeval_from_timespec(delay_timespec);
  if (adata->ev) {
    event_free(adata->ev);
    gpr_log(GPR_INFO, "Adding an alarm that already has an event.");
    adata->ev = NULL;
  }
  adata->ev = evtimer_new(g_event_base, libevent_alarm_cb, alarm);
  /* Set the trigger field to untriggered. Do this as the last store since
     it is a release of previous stores. */
  gpr_atm_rel_store(&alarm->triggered, ALARM_TRIGGER_INIT);

  return adata->ev != NULL && evtimer_add(adata->ev, &delay) == 0;
}

int grpc_alarm_cancel(grpc_alarm *alarm) {
  grpc_libevent_activation_data *adata =
      &alarm->task.activation[GRPC_EM_TA_ONLY];
  int trigger_old;

  /* First check if this alarm has been triggered, atomically */
  trigger_old =
      gpr_atm_full_fetch_add(&alarm->triggered, ALARM_TRIGGER_INCREMENT);
  if (trigger_old == ALARM_TRIGGER_INIT) {
    /* We need to make sure that we only invoke the callback if it hasn't
       already been invoked */
    /* First remove this event from libevent. This returns success even if the
       event has gone active or invoked its callback. */
    if (evtimer_del(adata->ev) != 0) {
      /* The delete was unsuccessful for some reason. */
      gpr_log(GPR_ERROR, "Attempt to delete alarm event was unsuccessful");
      return 0;
    }
    /* Free up the event structure before invoking callback */
    alarm_ev_destroy(alarm);
    adata->status = GRPC_CALLBACK_CANCELLED;
    add_task(adata);
  }
  return 1;
}

static void grpc_fd_impl_destroy(grpc_fd *impl) {
  grpc_em_task_activity_type type;
  grpc_libevent_activation_data *adata;

  for (type = GRPC_EM_TA_READ; type < GRPC_EM_TA_COUNT; type++) {
    adata = &(impl->task.activation[type]);
    GPR_ASSERT(adata->next == NULL);
    if (adata->ev != NULL) {
      event_free(adata->ev);
      adata->ev = NULL;
    }
  }

  if (impl->shutdown_ev != NULL) {
    event_free(impl->shutdown_ev);
    impl->shutdown_ev = NULL;
  }
  gpr_mu_destroy(&impl->mu);
  close(impl->fd);
}

/* Proxy callback to call a gRPC read/write callback */
static void em_fd_cb(int fd, short what, void *arg /*=em_fd*/) {
  grpc_fd *em_fd = arg;
  grpc_iomgr_cb_status status = GRPC_CALLBACK_SUCCESS;
  int run_read_cb = 0;
  int run_write_cb = 0;
  grpc_libevent_activation_data *rdata, *wdata;

  gpr_mu_lock(&em_fd->mu);
  if (em_fd->shutdown_started) {
    status = GRPC_CALLBACK_CANCELLED;
  } else if (status == GRPC_CALLBACK_SUCCESS && (what & EV_TIMEOUT)) {
    status = GRPC_CALLBACK_TIMED_OUT;
    /* TODO(klempner): This is broken if we are monitoring both read and write
       events on the same fd -- generating a spurious event is okay, but
       generating a spurious timeout is not. */
    what |= (EV_READ | EV_WRITE);
  }

  if (what & EV_READ) {
    switch (em_fd->read_state) {
      case GRPC_FD_WAITING:
        run_read_cb = 1;
        em_fd->read_state = GRPC_FD_IDLE;
        break;
      case GRPC_FD_IDLE:
      case GRPC_FD_CACHED:
        em_fd->read_state = GRPC_FD_CACHED;
    }
  }
  if (what & EV_WRITE) {
    switch (em_fd->write_state) {
      case GRPC_FD_WAITING:
        run_write_cb = 1;
        em_fd->write_state = GRPC_FD_IDLE;
        break;
      case GRPC_FD_IDLE:
      case GRPC_FD_CACHED:
        em_fd->write_state = GRPC_FD_CACHED;
    }
  }

  if (run_read_cb) {
    rdata = &(em_fd->task.activation[GRPC_EM_TA_READ]);
    rdata->status = status;
    add_task(rdata);
  } else if (run_write_cb) {
    wdata = &(em_fd->task.activation[GRPC_EM_TA_WRITE]);
    wdata->status = status;
    add_task(wdata);
  }
  gpr_mu_unlock(&em_fd->mu);
}

static void em_fd_shutdown_cb(int fd, short what, void *arg /*=em_fd*/) {
  /* TODO(klempner): This could just run directly in the calling thread, except
     that libevent's handling of event_active() on an event which is already in
     flight on a different thread is racy and easily triggers TSAN.
   */
  grpc_fd *impl = arg;
  gpr_mu_lock(&impl->mu);
  impl->shutdown_started = 1;
  if (impl->read_state == GRPC_FD_WAITING) {
    event_active(impl->task.activation[GRPC_EM_TA_READ].ev, EV_READ, 1);
  }
  if (impl->write_state == GRPC_FD_WAITING) {
    event_active(impl->task.activation[GRPC_EM_TA_WRITE].ev, EV_WRITE, 1);
  }
  gpr_mu_unlock(&impl->mu);
}

grpc_fd *grpc_fd_create(int fd) {
  int flags;
  grpc_libevent_activation_data *rdata, *wdata;
  grpc_fd *impl = gpr_malloc(sizeof(grpc_fd));

  gpr_mu_lock(&grpc_iomgr_mu);
  g_num_fds++;
  gpr_mu_unlock(&grpc_iomgr_mu);

  impl->shutdown_ev = NULL;
  gpr_mu_init(&impl->mu);

  flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT((flags & O_NONBLOCK) != 0);

  impl->task.type = GRPC_EM_TASK_FD;
  impl->fd = fd;

  rdata = &(impl->task.activation[GRPC_EM_TA_READ]);
  rdata->ev = NULL;
  rdata->cb = NULL;
  rdata->arg = NULL;
  rdata->status = GRPC_CALLBACK_SUCCESS;
  rdata->prev = NULL;
  rdata->next = NULL;

  wdata = &(impl->task.activation[GRPC_EM_TA_WRITE]);
  wdata->ev = NULL;
  wdata->cb = NULL;
  wdata->arg = NULL;
  wdata->status = GRPC_CALLBACK_SUCCESS;
  wdata->prev = NULL;
  wdata->next = NULL;

  impl->read_state = GRPC_FD_IDLE;
  impl->write_state = GRPC_FD_IDLE;

  impl->shutdown_started = 0;
  impl->next = NULL;

  /* TODO(chenw): detect platforms where only level trigger is supported,
     and set the event to non-persist. */
  rdata->ev = event_new(g_event_base, impl->fd, EV_ET | EV_PERSIST | EV_READ,
                        em_fd_cb, impl);
  GPR_ASSERT(rdata->ev);

  wdata->ev = event_new(g_event_base, impl->fd, EV_ET | EV_PERSIST | EV_WRITE,
                        em_fd_cb, impl);
  GPR_ASSERT(wdata->ev);

  impl->shutdown_ev =
      event_new(g_event_base, -1, EV_READ, em_fd_shutdown_cb, impl);
  GPR_ASSERT(impl->shutdown_ev);

  return impl;
}

void grpc_fd_destroy(grpc_fd *impl) {
  gpr_mu_lock(&grpc_iomgr_mu);

  if (g_num_pollers == 0) {
    /* it is safe to simply free it */
    grpc_fd_impl_destroy(impl);
    gpr_free(impl);
  } else {
    /* Put the impl on the list to be destroyed by the poller. */
    impl->next = g_fds_to_free;
    g_fds_to_free = impl;
    /* TODO(ctiller): kick the poller so it destroys this fd promptly
       (currently we may wait up to a second) */
  }

  g_num_fds--;
  gpr_cv_broadcast(&grpc_iomgr_cv);
  gpr_mu_unlock(&grpc_iomgr_mu);
}

int grpc_fd_get(struct grpc_fd *em_fd) { return em_fd->fd; }

/* TODO(chenw): should we enforce the contract that notify_on_read cannot be
   called when the previously registered callback has not been called yet. */
int grpc_fd_notify_on_read(grpc_fd *impl, grpc_iomgr_cb_func read_cb,
                           void *read_cb_arg, gpr_timespec deadline) {
  int force_event = 0;
  grpc_libevent_activation_data *rdata;
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  struct timeval delay = gpr_timeval_from_timespec(delay_timespec);
  struct timeval *delayp =
      gpr_time_cmp(deadline, gpr_inf_future) ? &delay : NULL;

  rdata = &impl->task.activation[GRPC_EM_TA_READ];

  gpr_mu_lock(&impl->mu);
  rdata->cb = read_cb;
  rdata->arg = read_cb_arg;

  force_event = (impl->shutdown_started || impl->read_state == GRPC_FD_CACHED);
  impl->read_state = GRPC_FD_WAITING;

  if (force_event) {
    event_active(rdata->ev, EV_READ, 1);
  } else if (event_add(rdata->ev, delayp) == -1) {
    gpr_mu_unlock(&impl->mu);
    return 0;
  }
  gpr_mu_unlock(&impl->mu);
  return 1;
}

int grpc_fd_notify_on_write(grpc_fd *impl, grpc_iomgr_cb_func write_cb,
                            void *write_cb_arg, gpr_timespec deadline) {
  int force_event = 0;
  grpc_libevent_activation_data *wdata;
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  struct timeval delay = gpr_timeval_from_timespec(delay_timespec);
  struct timeval *delayp =
      gpr_time_cmp(deadline, gpr_inf_future) ? &delay : NULL;

  wdata = &impl->task.activation[GRPC_EM_TA_WRITE];

  gpr_mu_lock(&impl->mu);
  wdata->cb = write_cb;
  wdata->arg = write_cb_arg;

  force_event = (impl->shutdown_started || impl->write_state == GRPC_FD_CACHED);
  impl->write_state = GRPC_FD_WAITING;

  if (force_event) {
    event_active(wdata->ev, EV_WRITE, 1);
  } else if (event_add(wdata->ev, delayp) == -1) {
    gpr_mu_unlock(&impl->mu);
    return 0;
  }
  gpr_mu_unlock(&impl->mu);
  return 1;
}

void grpc_fd_shutdown(grpc_fd *em_fd) {
  event_active(em_fd->shutdown_ev, EV_READ, 1);
}

/* Sometimes we want a followup callback: something to be added from the
   current callback for the EM to invoke once this callback is complete.
   This is implemented by inserting an entry into an EM queue. */

/* The following structure holds the field needed for adding the
   followup callback. These are the argument for the followup callback,
   the function to use for the followup callback, and the
   activation data pointer used for the queues (to free in the CB) */
struct followup_callback_arg {
  grpc_iomgr_cb_func func;
  void *cb_arg;
  grpc_libevent_activation_data adata;
};

static void followup_proxy_callback(void *cb_arg, grpc_iomgr_cb_status status) {
  struct followup_callback_arg *fcb_arg = cb_arg;
  /* Invoke the function */
  fcb_arg->func(fcb_arg->cb_arg, status);
  gpr_free(fcb_arg);
}

void grpc_iomgr_add_callback(grpc_iomgr_cb_func cb, void *cb_arg) {
  grpc_libevent_activation_data *adptr;
  struct followup_callback_arg *fcb_arg;

  fcb_arg = gpr_malloc(sizeof(*fcb_arg));
  /* Set up the activation data and followup callback argument structures */
  adptr = &fcb_arg->adata;
  adptr->ev = NULL;
  adptr->cb = followup_proxy_callback;
  adptr->arg = fcb_arg;
  adptr->status = GRPC_CALLBACK_SUCCESS;
  adptr->prev = NULL;
  adptr->next = NULL;

  fcb_arg->func = cb;
  fcb_arg->cb_arg = cb_arg;

  /* Insert an activation data for the specified em */
  add_task(adptr);
}
