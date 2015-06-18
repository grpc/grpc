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


PyMethodDef pygrpc_Server_methods[] = {
    {"request_call", (PyCFunction)pygrpc_Server_request_call,
     METH_KEYWORDS, ""},
    {"add_http2_port", (PyCFunction)pygrpc_Server_add_http2_port,
     METH_KEYWORDS, ""},
    {"start", (PyCFunction)pygrpc_Server_start, METH_NOARGS, ""},
    {"shutdown", (PyCFunction)pygrpc_Server_shutdown, METH_KEYWORDS, ""},
    {NULL}
};
const char pygrpc_Server_doc[] = "See grpc._adapter._types.Server.";
PyTypeObject pygrpc_Server_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "Server",                                 /* tp_name */
    sizeof(Server),                           /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_Server_dealloc,        /* tp_dealloc */
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
    pygrpc_Server_doc,                        /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_Server_methods,                    /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    0,                                        /* tp_alloc */
    (newfunc)pygrpc_Server_new                /* tp_new */
};

Server *pygrpc_Server_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  Server *self;
  CompletionQueue *cq;
  PyObject *py_args;
  grpc_channel_args c_args;
  char *keywords[] = {"cq", "args", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O:Channel", keywords,
        &pygrpc_CompletionQueue_type, &cq, &py_args)) {
    return NULL;
  }
  if (!pygrpc_produce_channel_args(py_args, &c_args)) {
    return NULL;
  }
  self = (Server *)type->tp_alloc(type, 0);
  self->c_serv = grpc_server_create(&c_args);
  pygrpc_discard_channel_args(c_args);
  self->cq = cq;
  Py_INCREF(self->cq);
  return self;
}

void pygrpc_Server_dealloc(Server *self) {
  grpc_server_destroy(self->c_serv);
  Py_XDECREF(self->cq);
  self->ob_type->tp_free((PyObject *)self);
}

PyObject *pygrpc_Server_request_call(
    Server *self, PyObject *args, PyObject *kwargs) {
  CompletionQueue *cq;
  PyObject *user_tag;
  pygrpc_tag *tag;
  Call *empty_call;
  grpc_call_error errcode;
  static char *keywords[] = {"cq", "tag", NULL};
  if (!PyArg_ParseTupleAndKeywords(
      args, kwargs, "O!O", keywords,
      &pygrpc_CompletionQueue_type, &cq, &user_tag)) {
    return NULL;
  }
  empty_call = pygrpc_Call_new_empty(cq);
  tag = pygrpc_produce_request_tag(user_tag, empty_call);
  errcode = grpc_server_request_call(
      self->c_serv, &tag->call->c_call, &tag->request_call_details,
      &tag->request_metadata, tag->call->cq->c_cq, self->cq->c_cq, tag);
  Py_DECREF(empty_call);
  return PyInt_FromLong(errcode);
}

PyObject *pygrpc_Server_add_http2_port(
    Server *self, PyObject *args, PyObject *kwargs) {
  const char *addr;
  ServerCredentials *creds = NULL;
  int port;
  static char *keywords[] = {"addr", "creds", NULL};
  if (!PyArg_ParseTupleAndKeywords(
      args, kwargs, "s|O!:add_http2_port", keywords,
      &addr, &pygrpc_ServerCredentials_type, &creds)) {
    return NULL;
  }
  if (creds) {
    port = grpc_server_add_secure_http2_port(
        self->c_serv, addr, creds->c_creds);
  } else {
    port = grpc_server_add_http2_port(self->c_serv, addr);
  }
  return PyInt_FromLong(port);

}

PyObject *pygrpc_Server_start(Server *self, PyObject *ignored) {
  grpc_server_start(self->c_serv);
  Py_RETURN_NONE;
}

PyObject *pygrpc_Server_shutdown(
    Server *self, PyObject *args, PyObject *kwargs) {
  PyObject *user_tag;
  pygrpc_tag *tag;
  static char *keywords[] = {"tag", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", keywords, &user_tag)) {
    return NULL;
  }
  tag = pygrpc_produce_server_shutdown_tag(user_tag);
  grpc_server_shutdown_and_notify(self->c_serv, self->cq->c_cq, tag);
  Py_RETURN_NONE;
}
