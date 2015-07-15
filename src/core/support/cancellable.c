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

/* Implementation for gpr_cancellable */

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

void gpr_cancellable_init(gpr_cancellable *c) {
  gpr_mu_init(&c->mu);
  c->cancelled = 0;
  c->waiters.next = &c->waiters;
  c->waiters.prev = &c->waiters;
  c->waiters.mu = NULL;
  c->waiters.cv = NULL;
}

void gpr_cancellable_destroy(gpr_cancellable *c) { gpr_mu_destroy(&c->mu); }

int gpr_cancellable_is_cancelled(gpr_cancellable *c) {
  return gpr_atm_acq_load(&c->cancelled) != 0;
}

/* Threads in gpr_cv_cancellable_wait(cv, mu, ..., c) place themselves on a
   linked list c->waiters of gpr_cancellable_list_ before waiting on their
   condition variables.  They check for cancellation while holding *mu.  Thus,
   to wake a thread from gpr_cv_cancellable_wait(), it suffices to:
      - set c->cancelled
      - acquire and release *mu
      - gpr_cv_broadcast(cv)

   However, gpr_cancellable_cancel() may not use gpr_mu_lock(mu), since the
   caller may already hold *mu---a possible deadlock.  (If we knew the caller
   did not hold *mu, care would still be needed, because c->mu follows *mu in
   the locking order, so *mu could not be acquired while holding c->mu---which
   is needed to iterate over c->waiters.)

   Therefore, gpr_cancellable_cancel() uses gpr_mu_trylock() rather than
   gpr_mu_lock(), and retries until either gpr_mu_trylock() succeeds or the
   thread leaves gpr_cv_cancellable_wait() for other reasons.  In the first
   case, gpr_cancellable_cancel() removes the entry from the waiters list; in
   the second, the waiting thread removes itself from the list.

   A one-entry cache of mutexes and condition variables processed is kept to
   avoid doing the same work again and again if many threads are blocked in the
   same place.  However, it's important to broadcast on a condition variable if
   the corresponding mutex has been locked successfully, even if the condition
   variable has been signalled before.  */

void gpr_cancellable_cancel(gpr_cancellable *c) {
  if (!gpr_cancellable_is_cancelled(c)) {
    int failures;
    int backoff = 1;
    do {
      struct gpr_cancellable_list_ *l;
      struct gpr_cancellable_list_ *nl;
      gpr_mu *omu = 0; /* one-element cache of a processed gpr_mu */
      gpr_cv *ocv = 0; /* one-element cache of a processd gpr_cv */
      gpr_mu_lock(&c->mu);
      gpr_atm_rel_store(&c->cancelled, 1);
      failures = 0;
      for (l = c->waiters.next; l != &c->waiters; l = nl) {
        nl = l->next;
        if (omu != l->mu) {
          omu = l->mu;
          if (gpr_mu_trylock(l->mu)) {
            gpr_mu_unlock(l->mu);
            l->next->prev = l->prev; /* remove *l from list */
            l->prev->next = l->next;
            /* allow unconditional dequeue in gpr_cv_cancellable_wait() */
            l->next = l;
            l->prev = l;
            ocv = 0; /* force broadcast */
          } else {
            failures++;
          }
        }
        if (ocv != l->cv) {
          ocv = l->cv;
          gpr_cv_broadcast(l->cv);
        }
      }
      gpr_mu_unlock(&c->mu);
      if (failures != 0) {
        if (backoff < 10) {
          volatile int i;
          for (i = 0; i != (1 << backoff); i++) {
          }
          backoff++;
        } else {
          gpr_event ev;
          gpr_event_init(&ev);
          gpr_event_wait(&ev, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                           gpr_time_from_micros(1000)));
        }
      }
    } while (failures != 0);
  }
}

int gpr_cv_cancellable_wait(gpr_cv *cv, gpr_mu *mu, gpr_timespec abs_deadline,
                            gpr_cancellable *c) {
  gpr_int32 timeout;
  gpr_mu_lock(&c->mu);
  timeout = gpr_cancellable_is_cancelled(c);
  if (!timeout) {
    struct gpr_cancellable_list_ le;
    le.mu = mu;
    le.cv = cv;
    le.next = c->waiters.next;
    le.prev = &c->waiters;
    le.next->prev = &le;
    le.prev->next = &le;
    gpr_mu_unlock(&c->mu);
    timeout = gpr_cv_wait(cv, mu, abs_deadline);
    gpr_mu_lock(&c->mu);
    le.next->prev = le.prev;
    le.prev->next = le.next;
    if (!timeout) {
      timeout = gpr_cancellable_is_cancelled(c);
    }
  }
  gpr_mu_unlock(&c->mu);
  return timeout;
}
