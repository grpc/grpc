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

#include "src/core/eventmanager/em.h"

#include <unistd.h>
#include <fcntl.h>

#include <grpc/support/atm.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <event2/event.h>
#include <event2/thread.h>

int evthread_use_threads(void);

static void grpc_em_fd_impl_destroy(struct grpc_em_fd_impl *impl);

#define ALARM_TRIGGER_INIT ((gpr_atm)0)
#define ALARM_TRIGGER_INCREMENT ((gpr_atm)1)
#define DONE_SHUTDOWN ((void *)1)

#define POLLER_ID_INVALID ((gpr_atm)-1)

typedef struct grpc_em_fd_impl {
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

  /* descriptor delete list. These are destroyed during polling. */
  struct grpc_em_fd_impl *next;
} grpc_em_fd_impl;

/* ================== grpc_em implementation ===================== */

/* If anything is in the work queue, process one item and return 1.
   Return 0 if there were no work items to complete.
   Requires em->mu locked, may unlock and relock during the call. */
static int maybe_do_queue_work(grpc_em *em) {
  grpc_em_activation_data *work = em->q;

  if (work == NULL) return 0;

  if (work->next == work) {
    em->q = NULL;
  } else {
    em->q = work->next;
    em->q->prev = work->prev;
    em->q->next->prev = em->q->prev->next = em->q;
  }
  work->next = work->prev = NULL;
  gpr_mu_unlock(&em->mu);

  work->cb(work->arg, work->status);

  gpr_mu_lock(&em->mu);
  return 1;
}

/* Break out of the event loop on timeout */
static void timer_callback(int fd, short events, void *context) {
  event_base_loopbreak((struct event_base *)context);
}

static void free_fd_list(grpc_em_fd_impl *impl) {
  while (impl != NULL) {
    grpc_em_fd_impl *current = impl;
    impl = impl->next;
    grpc_em_fd_impl_destroy(current);
    gpr_free(current);
  }
}

/* Spend some time doing polling and libevent maintenance work if no other
   thread is. This includes both polling for events and destroying/closing file
   descriptor objects.
   Returns 1 if polling was performed, 0 otherwise.
   Requires em->mu locked, may unlock and relock during the call. */
static int maybe_do_polling_work(grpc_em *em, struct timeval delay) {
  int status;

  if (em->num_pollers) return 0;

  em->num_pollers = 1;

  free_fd_list(em->fds_to_free);
  em->fds_to_free = NULL;

  gpr_mu_unlock(&em->mu);

  event_add(em->timeout_ev, &delay);
  status = event_base_loop(em->event_base, EVLOOP_ONCE);
  if (status < 0) {
    gpr_log(GPR_ERROR, "event polling loop stops with error status %d", status);
  }
  event_del(em->timeout_ev);

  gpr_mu_lock(&em->mu);
  if (em->fds_to_free) {
    free_fd_list(em->fds_to_free);
    em->fds_to_free = NULL;
  }

  em->num_pollers = 0;
  gpr_cv_broadcast(&em->cv);
  return 1;
}

int grpc_em_work(grpc_em *em, gpr_timespec deadline) {
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  /* poll for no longer than one second */
  gpr_timespec max_delay = {1, 0};
  struct timeval delay;

  GPR_ASSERT(em);

  if (gpr_time_cmp(delay_timespec, gpr_time_0) <= 0) {
    return 0;
  }

  if (gpr_time_cmp(delay_timespec, max_delay) > 0) {
    delay_timespec = max_delay;
  }

  delay = gpr_timeval_from_timespec(delay_timespec);

  if (maybe_do_queue_work(em) || maybe_do_polling_work(em, delay)) {
    em->last_poll_completed = gpr_now();
    return 1;
  }

  return 0;
}

static void backup_poller_thread(void *p) {
  grpc_em *em = p;
  int backup_poller_engaged = 0;
  /* allow no pollers for 100 milliseconds, then engage backup polling */
  gpr_timespec allow_no_pollers = gpr_time_from_micros(100 * 1000);

  gpr_mu_lock(&em->mu);
  while (!em->shutdown_backup_poller) {
    if (em->num_pollers == 0) {
      gpr_timespec now = gpr_now();
      gpr_timespec time_until_engage = gpr_time_sub(
          allow_no_pollers, gpr_time_sub(now, em->last_poll_completed));
      if (gpr_time_cmp(time_until_engage, gpr_time_0) <= 0) {
        if (!backup_poller_engaged) {
          gpr_log(GPR_DEBUG, "No pollers for a while - engaging backup poller");
          backup_poller_engaged = 1;
        }
        if (!maybe_do_queue_work(em)) {
          struct timeval tv = {1, 0};
          maybe_do_polling_work(em, tv);
        }
      } else {
        if (backup_poller_engaged) {
          gpr_log(GPR_DEBUG, "Backup poller disengaged");
          backup_poller_engaged = 0;
        }
        gpr_mu_unlock(&em->mu);
        gpr_sleep_until(gpr_time_add(now, time_until_engage));
        gpr_mu_lock(&em->mu);
      }
    } else {
      if (backup_poller_engaged) {
        gpr_log(GPR_DEBUG, "Backup poller disengaged");
        backup_poller_engaged = 0;
      }
      gpr_cv_wait(&em->cv, &em->mu, gpr_inf_future);
    }
  }
  gpr_mu_unlock(&em->mu);

  gpr_event_set(&em->backup_poller_done, (void *)1);
}

grpc_em_error grpc_em_init(grpc_em *em) {
  gpr_thd_id backup_poller_id;

  if (evthread_use_threads() != 0) {
    gpr_log(GPR_ERROR, "Failed to initialize libevent thread support!");
    return GRPC_EM_ERROR;
  }

  gpr_mu_init(&em->mu);
  gpr_cv_init(&em->cv);
  em->q = NULL;
  em->num_pollers = 0;
  em->num_fds = 0;
  em->last_poll_completed = gpr_now();
  em->shutdown_backup_poller = 0;
  em->fds_to_free = NULL;

  gpr_event_init(&em->backup_poller_done);

  em->event_base = NULL;
  em->timeout_ev = NULL;

  em->event_base = event_base_new();
  if (!em->event_base) {
    gpr_log(GPR_ERROR, "Failed to create the event base");
    return GRPC_EM_ERROR;
  }

  if (evthread_make_base_notifiable(em->event_base) != 0) {
    gpr_log(GPR_ERROR, "Couldn't make event base notifiable cross threads!");
    return GRPC_EM_ERROR;
  }

  em->timeout_ev = evtimer_new(em->event_base, timer_callback, em->event_base);

  gpr_thd_new(&backup_poller_id, backup_poller_thread, em, NULL);

  return GRPC_EM_OK;
}

grpc_em_error grpc_em_destroy(grpc_em *em) {
  gpr_timespec fd_shutdown_deadline =
      gpr_time_add(gpr_now(), gpr_time_from_micros(10 * 1000 * 1000));

  /* broadcast shutdown */
  gpr_mu_lock(&em->mu);
  while (em->num_fds) {
    gpr_log(GPR_INFO,
            "waiting for %d fds to be destroyed before closing event manager",
            em->num_fds);
    if (gpr_cv_wait(&em->cv, &em->mu, fd_shutdown_deadline)) {
      gpr_log(GPR_ERROR,
              "not all fds destroyed before shutdown deadline: memory leaks "
              "are likely");
      break;
    } else if (em->num_fds == 0) {
      gpr_log(GPR_INFO, "all fds closed");
    }
  }

  em->shutdown_backup_poller = 1;
  gpr_cv_broadcast(&em->cv);
  gpr_mu_unlock(&em->mu);

  gpr_event_wait(&em->backup_poller_done, gpr_inf_future);

  /* drain pending work */
  gpr_mu_lock(&em->mu);
  while (maybe_do_queue_work(em))
    ;
  gpr_mu_unlock(&em->mu);

  free_fd_list(em->fds_to_free);

  /* complete shutdown */
  gpr_mu_destroy(&em->mu);
  gpr_cv_destroy(&em->cv);

  if (em->timeout_ev != NULL) {
    event_free(em->timeout_ev);
  }

  if (em->event_base != NULL) {
    event_base_free(em->event_base);
    em->event_base = NULL;
  }

  return GRPC_EM_OK;
}

static void add_task(grpc_em *em, grpc_em_activation_data *adata) {
  gpr_mu_lock(&em->mu);
  if (em->q) {
    adata->next = em->q;
    adata->prev = adata->next->prev;
    adata->next->prev = adata->prev->next = adata;
  } else {
    em->q = adata;
    adata->next = adata->prev = adata;
  }
  gpr_cv_broadcast(&em->cv);
  gpr_mu_unlock(&em->mu);
}

/* ===============grpc_em_alarm implementation==================== */

/* The following function frees up the alarm's libevent structure and
   should always be invoked just before calling the alarm's callback */
static void alarm_ev_destroy(grpc_em_alarm *alarm) {
  grpc_em_activation_data *adata = &alarm->task.activation[GRPC_EM_TA_ONLY];
  if (adata->ev != NULL) {
    /* TODO(klempner): Is this safe to do when we're cancelling? */
    event_free(adata->ev);
    adata->ev = NULL;
  }
}
/* Proxy callback triggered by alarm->ev to call alarm->cb */
static void libevent_alarm_cb(int fd, short what, void *arg /*=alarm*/) {
  grpc_em_alarm *alarm = arg;
  grpc_em_activation_data *adata = &alarm->task.activation[GRPC_EM_TA_ONLY];
  int trigger_old;

  /* First check if this alarm has been canceled, atomically */
  trigger_old =
      gpr_atm_full_fetch_add(&alarm->triggered, ALARM_TRIGGER_INCREMENT);
  if (trigger_old == ALARM_TRIGGER_INIT) {
    /* Before invoking user callback, destroy the libevent structure */
    alarm_ev_destroy(alarm);
    adata->status = GRPC_CALLBACK_SUCCESS;
    add_task(alarm->task.em, adata);
  }
}

grpc_em_error grpc_em_alarm_init(grpc_em_alarm *alarm, grpc_em *em,
                                 grpc_em_cb_func alarm_cb, void *alarm_cb_arg) {
  grpc_em_activation_data *adata = &alarm->task.activation[GRPC_EM_TA_ONLY];
  alarm->task.type = GRPC_EM_TASK_ALARM;
  alarm->task.em = em;
  gpr_atm_rel_store(&alarm->triggered, ALARM_TRIGGER_INIT);
  adata->cb = alarm_cb;
  adata->arg = alarm_cb_arg;
  adata->prev = NULL;
  adata->next = NULL;
  adata->ev = NULL;
  return GRPC_EM_OK;
}

grpc_em_error grpc_em_alarm_add(grpc_em_alarm *alarm, gpr_timespec deadline) {
  grpc_em_activation_data *adata = &alarm->task.activation[GRPC_EM_TA_ONLY];
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  struct timeval delay = gpr_timeval_from_timespec(delay_timespec);
  if (adata->ev) {
    event_free(adata->ev);
    gpr_log(GPR_INFO, "Adding an alarm that already has an event.");
    adata->ev = NULL;
  }
  adata->ev = evtimer_new(alarm->task.em->event_base, libevent_alarm_cb, alarm);
  /* Set the trigger field to untriggered. Do this as the last store since
     it is a release of previous stores. */
  gpr_atm_rel_store(&alarm->triggered, ALARM_TRIGGER_INIT);

  if (adata->ev != NULL && evtimer_add(adata->ev, &delay) == 0) {
    return GRPC_EM_OK;
  } else {
    return GRPC_EM_ERROR;
  }
}

grpc_em_error grpc_em_alarm_cancel(grpc_em_alarm *alarm) {
  grpc_em_activation_data *adata = &alarm->task.activation[GRPC_EM_TA_ONLY];
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
      return GRPC_EM_ERROR;
    }
    /* Free up the event structure before invoking callback */
    alarm_ev_destroy(alarm);
    adata->status = GRPC_CALLBACK_CANCELLED;
    add_task(alarm->task.em, adata);
  }
  return GRPC_EM_OK;
}

/* ==================== grpc_em_fd implementation =================== */

/* Proxy callback to call a gRPC read/write callback */
static void em_fd_cb(int fd, short what, void *arg /*=em_fd_impl*/) {
  grpc_em_fd_impl *em_fd = arg;
  grpc_em_cb_status status = GRPC_CALLBACK_SUCCESS;
  int run_read_cb = 0;
  int run_write_cb = 0;
  grpc_em_activation_data *rdata, *wdata;

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
      case GRPC_EM_FD_WAITING:
        run_read_cb = 1;
        em_fd->read_state = GRPC_EM_FD_IDLE;
        break;
      case GRPC_EM_FD_IDLE:
      case GRPC_EM_FD_CACHED:
        em_fd->read_state = GRPC_EM_FD_CACHED;
    }
  }
  if (what & EV_WRITE) {
    switch (em_fd->write_state) {
      case GRPC_EM_FD_WAITING:
        run_write_cb = 1;
        em_fd->write_state = GRPC_EM_FD_IDLE;
        break;
      case GRPC_EM_FD_IDLE:
      case GRPC_EM_FD_CACHED:
        em_fd->write_state = GRPC_EM_FD_CACHED;
    }
  }

  if (run_read_cb) {
    rdata = &(em_fd->task.activation[GRPC_EM_TA_READ]);
    rdata->status = status;
    add_task(em_fd->task.em, rdata);
  } else if (run_write_cb) {
    wdata = &(em_fd->task.activation[GRPC_EM_TA_WRITE]);
    wdata->status = status;
    add_task(em_fd->task.em, wdata);
  }
  gpr_mu_unlock(&em_fd->mu);
}

static void em_fd_shutdown_cb(int fd, short what, void *arg /*=em_fd*/) {
  /* TODO(klempner): This could just run directly in the calling thread, except
     that libevent's handling of event_active() on an event which is already in
     flight on a different thread is racy and easily triggers TSAN.
   */
  grpc_em_fd_impl *impl = arg;
  gpr_mu_lock(&impl->mu);
  impl->shutdown_started = 1;
  if (impl->read_state == GRPC_EM_FD_WAITING) {
    event_active(impl->task.activation[GRPC_EM_TA_READ].ev, EV_READ, 1);
  }
  if (impl->write_state == GRPC_EM_FD_WAITING) {
    event_active(impl->task.activation[GRPC_EM_TA_WRITE].ev, EV_WRITE, 1);
  }
  gpr_mu_unlock(&impl->mu);
}

grpc_em_error grpc_em_fd_init(grpc_em_fd *em_fd, grpc_em *em, int fd) {
  int flags;
  grpc_em_activation_data *rdata, *wdata;
  grpc_em_fd_impl *impl = gpr_malloc(sizeof(grpc_em_fd_impl));

  gpr_mu_lock(&em->mu);
  em->num_fds++;

  gpr_mu_unlock(&em->mu);

  em_fd->impl = impl;

  impl->shutdown_ev = NULL;
  gpr_mu_init(&impl->mu);

  flags = fcntl(fd, F_GETFL, 0);
  if ((flags & O_NONBLOCK) == 0) {
    gpr_log(GPR_ERROR, "File descriptor %d is blocking", fd);
    return GRPC_EM_INVALID_ARGUMENTS;
  }

  impl->task.type = GRPC_EM_TASK_FD;
  impl->task.em = em;
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

  impl->read_state = GRPC_EM_FD_IDLE;
  impl->write_state = GRPC_EM_FD_IDLE;

  impl->shutdown_started = 0;
  impl->next = NULL;

  /* TODO(chenw): detect platforms where only level trigger is supported,
     and set the event to non-persist. */
  rdata->ev = event_new(em->event_base, impl->fd, EV_ET | EV_PERSIST | EV_READ,
                        em_fd_cb, impl);
  if (!rdata->ev) {
    gpr_log(GPR_ERROR, "Failed to create read event");
    return GRPC_EM_ERROR;
  }

  wdata->ev = event_new(em->event_base, impl->fd, EV_ET | EV_PERSIST | EV_WRITE,
                        em_fd_cb, impl);
  if (!wdata->ev) {
    gpr_log(GPR_ERROR, "Failed to create write event");
    return GRPC_EM_ERROR;
  }

  impl->shutdown_ev =
      event_new(em->event_base, -1, EV_READ, em_fd_shutdown_cb, impl);

  if (!impl->shutdown_ev) {
    gpr_log(GPR_ERROR, "Failed to create shutdown event");
    return GRPC_EM_ERROR;
  }

  return GRPC_EM_OK;
}

static void grpc_em_fd_impl_destroy(grpc_em_fd_impl *impl) {
  grpc_em_task_activity_type type;
  grpc_em_activation_data *adata;

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

void grpc_em_fd_destroy(grpc_em_fd *em_fd) {
  grpc_em_fd_impl *impl = em_fd->impl;
  grpc_em *em = impl->task.em;

  gpr_mu_lock(&em->mu);

  if (em->num_pollers == 0) {
    /* it is safe to simply free it */
    grpc_em_fd_impl_destroy(impl);
    gpr_free(impl);
  } else {
    /* Put the impl on the list to be destroyed by the poller. */
    impl->next = em->fds_to_free;
    em->fds_to_free = impl;
    /* Kick the poller so it closes the fd promptly.
     * TODO(klempner): maybe this should be a different event.
     */
    event_active(em_fd->impl->shutdown_ev, EV_READ, 1);
  }

  em->num_fds--;
  gpr_cv_broadcast(&em->cv);
  gpr_mu_unlock(&em->mu);
}

int grpc_em_fd_get(struct grpc_em_fd *em_fd) { return em_fd->impl->fd; }

/* Returns the event manager associated with *em_fd. */
grpc_em *grpc_em_fd_get_em(grpc_em_fd *em_fd) { return em_fd->impl->task.em; }

/* TODO(chenw): should we enforce the contract that notify_on_read cannot be
   called when the previously registered callback has not been called yet. */
grpc_em_error grpc_em_fd_notify_on_read(grpc_em_fd *em_fd,
                                        grpc_em_cb_func read_cb,
                                        void *read_cb_arg,
                                        gpr_timespec deadline) {
  grpc_em_fd_impl *impl = em_fd->impl;
  int force_event = 0;
  grpc_em_activation_data *rdata;
  grpc_em_error result = GRPC_EM_OK;
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  struct timeval delay = gpr_timeval_from_timespec(delay_timespec);
  struct timeval *delayp =
      gpr_time_cmp(deadline, gpr_inf_future) ? &delay : NULL;

  rdata = &impl->task.activation[GRPC_EM_TA_READ];

  gpr_mu_lock(&impl->mu);
  rdata->cb = read_cb;
  rdata->arg = read_cb_arg;

  force_event =
      (impl->shutdown_started || impl->read_state == GRPC_EM_FD_CACHED);
  impl->read_state = GRPC_EM_FD_WAITING;

  if (force_event) {
    event_active(rdata->ev, EV_READ, 1);
  } else if (event_add(rdata->ev, delayp) == -1) {
    result = GRPC_EM_ERROR;
  }
  gpr_mu_unlock(&impl->mu);
  return result;
}

grpc_em_error grpc_em_fd_notify_on_write(grpc_em_fd *em_fd,
                                         grpc_em_cb_func write_cb,
                                         void *write_cb_arg,
                                         gpr_timespec deadline) {
  grpc_em_fd_impl *impl = em_fd->impl;
  int force_event = 0;
  grpc_em_activation_data *wdata;
  grpc_em_error result = GRPC_EM_OK;
  gpr_timespec delay_timespec = gpr_time_sub(deadline, gpr_now());
  struct timeval delay = gpr_timeval_from_timespec(delay_timespec);
  struct timeval *delayp =
      gpr_time_cmp(deadline, gpr_inf_future) ? &delay : NULL;

  wdata = &impl->task.activation[GRPC_EM_TA_WRITE];

  gpr_mu_lock(&impl->mu);
  wdata->cb = write_cb;
  wdata->arg = write_cb_arg;

  force_event =
      (impl->shutdown_started || impl->write_state == GRPC_EM_FD_CACHED);
  impl->write_state = GRPC_EM_FD_WAITING;

  if (force_event) {
    event_active(wdata->ev, EV_WRITE, 1);
  } else if (event_add(wdata->ev, delayp) == -1) {
    result = GRPC_EM_ERROR;
  }
  gpr_mu_unlock(&impl->mu);
  return result;
}

void grpc_em_fd_shutdown(grpc_em_fd *em_fd) {
  event_active(em_fd->impl->shutdown_ev, EV_READ, 1);
}

/*====================== Other callback functions ======================*/

/* Sometimes we want a followup callback: something to be added from the
   current callback for the EM to invoke once this callback is complete.
   This is implemented by inserting an entry into an EM queue. */

/* The following structure holds the field needed for adding the
   followup callback. These are the argument for the followup callback,
   the function to use for the followup callback, and the
   activation data pointer used for the queues (to free in the CB) */
struct followup_callback_arg {
  grpc_em_cb_func func;
  void *cb_arg;
  grpc_em_activation_data adata;
};

static void followup_proxy_callback(void *cb_arg, grpc_em_cb_status status) {
  struct followup_callback_arg *fcb_arg = cb_arg;
  /* Invoke the function */
  fcb_arg->func(fcb_arg->cb_arg, status);
  gpr_free(fcb_arg);
}

grpc_em_error grpc_em_add_callback(grpc_em *em, grpc_em_cb_func cb,
                                   void *cb_arg) {
  grpc_em_activation_data *adptr;
  struct followup_callback_arg *fcb_arg;

  fcb_arg = gpr_malloc(sizeof(*fcb_arg));
  if (fcb_arg == NULL) {
    return GRPC_EM_ERROR;
  }
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
  add_task(em, adptr);
  return GRPC_EM_OK;
}
