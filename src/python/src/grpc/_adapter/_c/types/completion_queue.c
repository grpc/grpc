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

#include "grpc/_adapter/_c/types.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <grpc/grpc.h>


PyMethodDef pygrpc_CompletionQueue_methods[] = {
    {"next", (PyCFunction)pygrpc_CompletionQueue_next, METH_KEYWORDS, ""},
    {"shutdown", (PyCFunction)pygrpc_CompletionQueue_shutdown, METH_NOARGS, ""},
    {NULL}
};
const char pygrpc_CompletionQueue_doc[] =
    "See grpc._adapter._types.CompletionQueue.";
PyTypeObject pygrpc_CompletionQueue_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "CompletionQueue",                        /* tp_name */
    sizeof(CompletionQueue),                  /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_CompletionQueue_dealloc, /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    0,                                        /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    pygrpc_CompletionQueue_doc,               /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_CompletionQueue_methods,           /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    0,                                        /* tp_alloc */
    (newfunc)pygrpc_CompletionQueue_new       /* tp_new */
};

CompletionQueue *pygrpc_CompletionQueue_new(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  CompletionQueue *self = (CompletionQueue *)type->tp_alloc(type, 0);
  self->c_cq = grpc_completion_queue_create();
  return self;
}

void pygrpc_CompletionQueue_dealloc(CompletionQueue *self) {
  grpc_completion_queue_destroy(self->c_cq);
  self->ob_type->tp_free((PyObject *)self);
}

PyObject *pygrpc_CompletionQueue_next(
    CompletionQueue *self, PyObject *args, PyObject *kwargs) {
  double deadline;
  grpc_event event;
  PyObject *transliterated_event;
  static char *keywords[] = {"deadline", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "d:next", keywords,
                                   &deadline)) {
    return NULL;
  }
  Py_BEGIN_ALLOW_THREADS;
  event = grpc_completion_queue_next(
      self->c_cq, pygrpc_cast_double_to_gpr_timespec(deadline));
  Py_END_ALLOW_THREADS;
  transliterated_event = pygrpc_consume_event(event);
  return transliterated_event;
}

PyObject *pygrpc_CompletionQueue_shutdown(
    CompletionQueue *self, PyObject *ignored) {
  grpc_completion_queue_shutdown(self->c_cq);
  Py_RETURN_NONE;
}
