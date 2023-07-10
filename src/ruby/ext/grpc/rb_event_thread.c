/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <ruby/ruby.h>

#include "rb_event_thread.h"

#include <ruby/thread.h>
#include <stdbool.h>

#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

typedef struct grpc_rb_event {
  // callback will be called with argument while holding the GVL
  void (*callback)(void*);
  void* argument;

  struct grpc_rb_event* next;
} grpc_rb_event;

typedef struct grpc_rb_event_queue {
  grpc_rb_event* head;
  grpc_rb_event* tail;

  gpr_mu mu;
  gpr_cv cv;

  // Indicates that the thread should stop waiting
  bool abort;
} grpc_rb_event_queue;

static grpc_rb_event_queue event_queue;
static VALUE g_event_thread = Qnil;
static bool g_one_time_init_done = false;

void grpc_rb_event_queue_enqueue(void (*callback)(void*), void* argument) {
  grpc_rb_event* event = gpr_malloc(sizeof(grpc_rb_event));
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

static grpc_rb_event* grpc_rb_event_queue_dequeue() {
  grpc_rb_event* event;
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

static void* grpc_rb_wait_for_event_no_gil(void* param) {
  grpc_rb_event* event = NULL;
  (void)param;
  gpr_mu_lock(&event_queue.mu);
  while (!event_queue.abort) {
    if ((event = grpc_rb_event_queue_dequeue()) != NULL) {
      gpr_mu_unlock(&event_queue.mu);
      return event;
    }
    gpr_cv_wait(&event_queue.cv, &event_queue.mu,
                gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&event_queue.mu);
  return NULL;
}

static void grpc_rb_event_unblocking_func(void* arg) {
  (void)arg;
  gpr_mu_lock(&event_queue.mu);
  event_queue.abort = true;
  gpr_cv_signal(&event_queue.cv);
  gpr_mu_unlock(&event_queue.mu);
}

/* This is the implementation of the thread that handles auth metadata plugin
 * events */
static VALUE grpc_rb_event_thread(VALUE arg) {
  grpc_rb_event* event;
  (void)arg;
  grpc_ruby_init();
  while (true) {
    event = (grpc_rb_event*)rb_thread_call_without_gvl(
        grpc_rb_wait_for_event_no_gil, NULL, grpc_rb_event_unblocking_func,
        NULL);
    if (event == NULL) {
      // Indicates that the thread needs to shut down
      break;
    } else {
      event->callback(event->argument);
      gpr_free(event);
    }
  }
  grpc_rb_event_queue_destroy();
  grpc_ruby_shutdown();
  return Qnil;
}

void grpc_rb_event_queue_thread_start() {
  if (!g_one_time_init_done) {
    g_one_time_init_done = true;
    gpr_mu_init(&event_queue.mu);
    gpr_cv_init(&event_queue.cv);
    rb_global_variable(&g_event_thread);
    event_queue.head = event_queue.tail = NULL;
  }
  event_queue.abort = false;
  GPR_ASSERT(!RTEST(g_event_thread));
  g_event_thread = rb_thread_create(grpc_rb_event_thread, NULL);
}

void grpc_rb_event_queue_thread_stop() {
  GPR_ASSERT(g_one_time_init_done);
  if (!RTEST(g_event_thread)) {
    gpr_log(GPR_ERROR,
            "GRPC_RUBY: call credentials thread stop: thread not running");
    return;
  }
  rb_thread_call_without_gvl(grpc_rb_event_unblocking_func, NULL, NULL, NULL);
  rb_funcall(g_event_thread, rb_intern("join"), 0);
  g_event_thread = Qnil;
}
