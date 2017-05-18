/*
 *
 * Copyright 2016, Google Inc.
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

#include <ruby/ruby.h>

#include "rb_grpc_imports.generated.h"
#include "rb_event_thread.h"

#include <stdbool.h>

#include <ruby/thread.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc/support/log.h>

typedef struct grpc_rb_event {
  // callback will be called with argument while holding the GVL
  void (*callback)(void*);
  void *argument;

  struct grpc_rb_event *next;
} grpc_rb_event;

typedef struct grpc_rb_event_queue {
  grpc_rb_event *head;
  grpc_rb_event *tail;

  gpr_mu mu;
  gpr_cv cv;

  // Indicates that the thread should stop waiting
  bool abort;
} grpc_rb_event_queue;

static grpc_rb_event_queue event_queue;

void grpc_rb_event_queue_enqueue(void (*callback)(void*),
                                 void *argument) {
  grpc_rb_event *event = gpr_malloc(sizeof(grpc_rb_event));
  event->callback = callback;
  event->argument = argument;
  event->next = NULL;
  gpr_mu_lock(&event_queue.mu);
  if (event_queue.tail == NULL) {
    event_queue.head = event_queue.tail = event;
  } else {
    event_queue.tail->next = event;
    event_queue.tail = event;
  }
  gpr_cv_signal(&event_queue.cv);
  gpr_mu_unlock(&event_queue.mu);
}

static grpc_rb_event *grpc_rb_event_queue_dequeue() {
  grpc_rb_event *event;
  if (event_queue.head == NULL) {
    event = NULL;
  } else {
    event = event_queue.head;
    if (event_queue.head->next == NULL) {
      event_queue.head = event_queue.tail = NULL;
    } else {
      event_queue.head = event_queue.head->next;
    }
  }
  return event;
}

static void grpc_rb_event_queue_destroy() {
  gpr_mu_destroy(&event_queue.mu);
  gpr_cv_destroy(&event_queue.cv);
}

static void *grpc_rb_wait_for_event_no_gil(void *param) {
  grpc_rb_event *event = NULL;
  (void)param;
  gpr_mu_lock(&event_queue.mu);
  while (!event_queue.abort) {
    if ((event = grpc_rb_event_queue_dequeue()) != NULL) {
      gpr_mu_unlock(&event_queue.mu);
      return event;
    }
    gpr_cv_wait(&event_queue.cv,
                &event_queue.mu,
                gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&event_queue.mu);
  return NULL;
}

static void grpc_rb_event_unblocking_func(void *arg) {
  (void)arg;
  gpr_mu_lock(&event_queue.mu);
  event_queue.abort = true;
  gpr_cv_signal(&event_queue.cv);
  gpr_mu_unlock(&event_queue.mu);
}

/* This is the implementation of the thread that handles auth metadata plugin
 * events */
static VALUE grpc_rb_event_thread(VALUE arg) {
  grpc_rb_event *event;
  (void)arg;
  while(true) {
    event = (grpc_rb_event*)rb_thread_call_without_gvl(
        grpc_rb_wait_for_event_no_gil, NULL,
        grpc_rb_event_unblocking_func, NULL);
    if (event == NULL) {
      // Indicates that the thread needs to shut down
      break;
    } else {
      event->callback(event->argument);
      gpr_free(event);
    }
  }
  grpc_rb_event_queue_destroy();
  return Qnil;
}

void grpc_rb_event_queue_thread_start() {
  event_queue.head = event_queue.tail = NULL;
  event_queue.abort = false;
  gpr_mu_init(&event_queue.mu);
  gpr_cv_init(&event_queue.cv);

  rb_thread_create(grpc_rb_event_thread, NULL);
}
