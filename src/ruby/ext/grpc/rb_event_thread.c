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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <ruby/thread.h>
#include <stdbool.h>

#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

typedef struct grpc_rb_event {
  // callback will be called with argument while holding the GVL
  void (*callback)(void*);
  void* argument;
  struct grpc_rb_event* next;
  // Store the proc to be marked
  VALUE proc;
} grpc_rb_event;

typedef struct grpc_rb_event_queue {
  grpc_rb_event* head;
  grpc_rb_event* tail;
  VALUE last_dequeued_proc; // We also mark this

  gpr_mu mu;
  gpr_cv cv;

  // Indicates that the thread should stop waiting
  bool abort;
} grpc_rb_event_queue;

/* grpc_rb_cEventQueue is the ruby class that wraps the event queue. */
static VALUE grpc_rb_cEventQueue = Qnil;

/* g_event_queue is the global instance of the event queue */
static VALUE g_event_queue = Qnil;

static VALUE g_event_thread = Qnil;
static bool g_one_time_init_done = false;

/* Free function for the event queue */
static void grpc_rb_event_queue_free(void* p) {
  grpc_rb_event_queue* queue = (grpc_rb_event_queue*)p;
  if (queue == NULL) return;

  /* Clean up remaining events */
  grpc_rb_event* event = queue->head;
  while (event != NULL) {
    grpc_rb_event* next = event->next;
    gpr_free(event);
    event = next;
  }

  gpr_mu_destroy(&queue->mu);
  gpr_cv_destroy(&queue->cv);

  xfree(queue);
}

/* Mark function for the event queue - marks all procs in the queue */
static void grpc_rb_event_queue_mark(void* p) {
  grpc_rb_event_queue* queue = (grpc_rb_event_queue*)p;
  if (queue == NULL) return;

  gpr_mu_lock(&queue->mu);
  grpc_rb_event* event = queue->head;
  while (event != NULL) {
    if (event->proc != Qnil) {
      rb_gc_mark(event->proc);
    }
    event = event->next;
  }
  if (queue->last_dequeued_proc && queue->last_dequeued_proc != Qnil) {
    rb_gc_mark(queue->last_dequeued_proc);
  }
  gpr_mu_unlock(&queue->mu);
}

/* Ruby data type for the event queue */
static const rb_data_type_t grpc_rb_event_queue_data_type = {
    "grpc_event_queue",
    {grpc_rb_event_queue_mark,
     grpc_rb_event_queue_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocate a new event queue instance */
static VALUE grpc_rb_event_queue_alloc(VALUE cls) {
  grpc_rb_event_queue* queue = ALLOC(grpc_rb_event_queue);

  queue->head = NULL;
  queue->tail = NULL;
  queue->abort = false;
  queue->last_dequeued_proc = Qnil;
  gpr_mu_init(&queue->mu);
  gpr_cv_init(&queue->cv);

  return TypedData_Wrap_Struct(cls, &grpc_rb_event_queue_data_type, queue);
}

void grpc_rb_event_queue_enqueue(void (*callback)(void*), void* argument) {
  grpc_rb_event_queue* queue;

  if (!RTEST(g_event_queue)) {
    rb_raise(rb_eRuntimeError, "Event queue not initialized");
    return;
  }

  TypedData_Get_Struct(g_event_queue, grpc_rb_event_queue,
                       &grpc_rb_event_queue_data_type, queue);

  grpc_rb_event* event = gpr_malloc(sizeof(grpc_rb_event));
  event->callback = callback;
  event->argument = argument;
  event->next = NULL;

  /* For credential callbacks, the argument is a callback_params struct
   * where the first field is the Ruby proc (VALUE get_metadata).
   * We can safely extract it as the first VALUE in the struct. */
  if (argument != NULL) {
    VALUE* first_value = (VALUE*)argument;
    event->proc = *first_value;
  } else {
    event->proc = Qnil;
  }

  gpr_mu_lock(&queue->mu);
  if (queue->tail == NULL) {
    queue->head = queue->tail = event;
  } else {
    queue->tail->next = event;
    queue->tail = event;
  }
  gpr_cv_signal(&queue->cv);
  gpr_mu_unlock(&queue->mu);
}

static grpc_rb_event* grpc_rb_event_queue_dequeue(grpc_rb_event_queue* queue) {
  grpc_rb_event* event;
  if (queue->head == NULL) {
    event = NULL;
  } else {
    event = queue->head;
    if (queue->head->next == NULL) {
      queue->head = queue->tail = NULL;
    } else {
      queue->head = queue->head->next;
    }
  }
  if (event) {
    queue->last_dequeued_proc = event->proc;
  } else {
    queue->last_dequeued_proc = Qnil;
  }
  return event;
}

static void* grpc_rb_wait_for_event_no_gil(void* param) {
  grpc_rb_event_queue* queue = (grpc_rb_event_queue*)param;
  grpc_rb_event* event = NULL;

  gpr_mu_lock(&queue->mu);
  while (!queue->abort) {
    if ((event = grpc_rb_event_queue_dequeue(queue)) != NULL) {
      gpr_mu_unlock(&queue->mu);
      return event;
    }
    gpr_cv_wait(&queue->cv, &queue->mu,
                gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&queue->mu);
  return NULL;
}

static void* grpc_rb_event_unblocking_func_wrapper(void* arg) {
  grpc_rb_event_queue* queue = (grpc_rb_event_queue*)arg;

  gpr_mu_lock(&queue->mu);
  queue->abort = true;
  gpr_cv_signal(&queue->cv);
  gpr_mu_unlock(&queue->mu);
  return NULL;
}

static void grpc_rb_event_unblocking_func(void* arg) {
  grpc_rb_event_unblocking_func_wrapper(arg);
}

/* This is the implementation of the thread that handles auth metadata plugin
 * events */
static VALUE grpc_rb_event_thread(void* arg) {
  grpc_rb_event_queue* queue;
  grpc_rb_event* event;
  (void)arg;

  TypedData_Get_Struct(g_event_queue, grpc_rb_event_queue,
                       &grpc_rb_event_queue_data_type, queue);

  while (true) {
    event = (grpc_rb_event*)rb_thread_call_without_gvl(
        grpc_rb_wait_for_event_no_gil, queue, grpc_rb_event_unblocking_func,
        queue);
    if (event == NULL) {
      // Indicates that the thread needs to shut down
      break;
    } else {
      event->callback(event->argument);
      gpr_free(event);
    }
  }
  return Qnil;
}

void Init_grpc_event_queue() {
  grpc_rb_cEventQueue = rb_define_class_under(grpc_rb_mGrpcCore, "EventQueue",
                                             rb_cObject);
  rb_define_alloc_func(grpc_rb_cEventQueue, grpc_rb_event_queue_alloc);
}

void grpc_rb_event_queue_thread_start() {
  grpc_rb_event_queue* queue;

  if (!g_one_time_init_done) {
    g_one_time_init_done = true;
    Init_grpc_event_queue();

    g_event_queue = grpc_rb_event_queue_alloc(grpc_rb_cEventQueue);
    rb_global_variable(&g_event_queue);
    rb_global_variable(&g_event_thread);
  }

  TypedData_Get_Struct(g_event_queue, grpc_rb_event_queue,
                       &grpc_rb_event_queue_data_type, queue);

  GRPC_RUBY_ASSERT(!RTEST(g_event_thread));
  g_event_thread = rb_thread_create(grpc_rb_event_thread, NULL);
}

void grpc_rb_event_queue_thread_stop() {
  grpc_rb_event_queue* queue;

  GRPC_RUBY_ASSERT(g_one_time_init_done);
  if (!RTEST(g_event_thread)) {
    grpc_absl_log(
        GPR_ERROR,
        "GRPC_RUBY: call credentials thread stop: thread not running");
    return;
  }

  TypedData_Get_Struct(g_event_queue, grpc_rb_event_queue,
                       &grpc_rb_event_queue_data_type, queue);

  rb_thread_call_without_gvl(grpc_rb_event_unblocking_func_wrapper, queue,
                             NULL, NULL);
  rb_funcall(g_event_thread, rb_intern("join"), 0);
  g_event_thread = Qnil;
  g_event_queue = Qnil;
}
