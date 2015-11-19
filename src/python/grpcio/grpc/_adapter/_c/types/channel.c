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
#include <grpc/support/alloc.h>


PyMethodDef pygrpc_Channel_methods[] = {
    {"create_call", (PyCFunction)pygrpc_Channel_create_call, METH_KEYWORDS, ""},
    {"check_connectivity_state", (PyCFunction)pygrpc_Channel_check_connectivity_state, METH_KEYWORDS, ""},
    {"watch_connectivity_state", (PyCFunction)pygrpc_Channel_watch_connectivity_state, METH_KEYWORDS, ""},
    {"target", (PyCFunction)pygrpc_Channel_target, METH_NOARGS, ""},
    {NULL}
};
const char pygrpc_Channel_doc[] = "See grpc._adapter._types.Channel.";
PyTypeObject pygrpc_Channel_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "Channel",                                /* tp_name */
    sizeof(Channel),                          /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_Channel_dealloc,       /* tp_dealloc */
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
    pygrpc_Channel_doc,                       /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_Channel_methods,                   /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    0,                                        /* tp_alloc */
    (newfunc)pygrpc_Channel_new               /* tp_new */
};

Channel *pygrpc_Channel_new(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  Channel *self;
  const char *target;
  PyObject *py_args;
  ChannelCredentials *creds = NULL;
  grpc_channel_args c_args;
  char *keywords[] = {"target", "args", "creds", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO|O!:Channel", keywords,
        &target, &py_args, &pygrpc_ChannelCredentials_type, &creds)) {
    return NULL;
  }
  if (!pygrpc_produce_channel_args(py_args, &c_args)) {
    return NULL;
  }
  self = (Channel *)type->tp_alloc(type, 0);
  if (creds) {
    self->c_chan =
        grpc_secure_channel_create(creds->c_creds, target, &c_args, NULL);
  } else {
    self->c_chan = grpc_insecure_channel_create(target, &c_args, NULL);
  }
  pygrpc_discard_channel_args(c_args);
  return self;
}
void pygrpc_Channel_dealloc(Channel *self) {
  grpc_channel_destroy(self->c_chan);
  self->ob_type->tp_free((PyObject *)self);
}

Call *pygrpc_Channel_create_call(
    Channel *self, PyObject *args, PyObject *kwargs) {
  Call *call;
  CompletionQueue *cq;
  const char *method;
  const char *host;
  double deadline;
  char *keywords[] = {"cq", "method", "host", "deadline", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!szd:create_call", keywords,
        &pygrpc_CompletionQueue_type, &cq, &method, &host, &deadline)) {
    return NULL;
  }
  call = pygrpc_Call_new_empty(cq);
  call->c_call = grpc_channel_create_call(
      self->c_chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq->c_cq, method, host,
      pygrpc_cast_double_to_gpr_timespec(deadline), NULL);
  return call;
}

PyObject *pygrpc_Channel_check_connectivity_state(
    Channel *self, PyObject *args, PyObject *kwargs) {
  PyObject *py_try_to_connect;
  int try_to_connect;
  char *keywords[] = {"try_to_connect", NULL};
  grpc_connectivity_state state;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:connectivity_state", keywords,
                                   &py_try_to_connect)) {
    return NULL;
  }
  if (!PyBool_Check(py_try_to_connect)) {
    Py_XDECREF(py_try_to_connect);
    return NULL;
  }
  try_to_connect = Py_True == py_try_to_connect;
  Py_DECREF(py_try_to_connect);
  state = grpc_channel_check_connectivity_state(self->c_chan, try_to_connect);
  return PyInt_FromLong(state);
}

PyObject *pygrpc_Channel_watch_connectivity_state(
    Channel *self, PyObject *args, PyObject *kwargs) {
  PyObject *tag;
  double deadline;
  int last_observed_state;
  CompletionQueue *completion_queue;
  char *keywords[] = {"last_observed_state", "deadline",
                      "completion_queue", "tag", NULL};
  if (!PyArg_ParseTupleAndKeywords(
      args, kwargs, "idO!O:watch_connectivity_state", keywords,
      &last_observed_state, &deadline, &pygrpc_CompletionQueue_type,
      &completion_queue, &tag)) {
    return NULL;
  }
  grpc_channel_watch_connectivity_state(
      self->c_chan, (grpc_connectivity_state)last_observed_state,
      pygrpc_cast_double_to_gpr_timespec(deadline), completion_queue->c_cq,
      pygrpc_produce_channel_state_change_tag(tag));
  Py_RETURN_NONE;
}

PyObject *pygrpc_Channel_target(Channel *self) {
  char *target = grpc_channel_get_target(self->c_chan);
  PyObject *py_target = PyString_FromString(target);
  gpr_free(target);
  return py_target;
}
