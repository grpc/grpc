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

#include "_adapter/_server.h"

#include <Python.h>
#include <grpc/grpc.h>

#include "_adapter/_completion_queue.h"
#include "_adapter/_error.h"
#include "_adapter/_server_credentials.h"

static int pygrpc_server_init(Server *self, PyObject *args, PyObject *kwds) {
  const PyObject *completion_queue;
  PyObject *server_credentials;
  if (!(PyArg_ParseTuple(args, "O!O", &pygrpc_CompletionQueueType,
                         &completion_queue, &server_credentials))) {
    self->c_server = NULL;
    return -1;
  }
  if (server_credentials == Py_None) {
    self->c_server = grpc_server_create(
        ((CompletionQueue *)completion_queue)->c_completion_queue, NULL);
    return 0;
  } else if (PyObject_TypeCheck(server_credentials,
                                &pygrpc_ServerCredentialsType)) {
    self->c_server = grpc_secure_server_create(
        ((ServerCredentials *)server_credentials)->c_server_credentials,
        ((CompletionQueue *)completion_queue)->c_completion_queue, NULL);
    return 0;
  } else {
    self->c_server = NULL;
    return -1;
  }
}

static void pygrpc_server_dealloc(Server *self) {
  if (self->c_server != NULL) {
    grpc_server_destroy(self->c_server);
  }
  self->ob_type->tp_free((PyObject *)self);
}

static PyObject *pygrpc_server_add_http2_addr(Server *self, PyObject *args) {
  const char *addr;
  int port;
  PyArg_ParseTuple(args, "s", &addr);

  port = grpc_server_add_http2_port(self->c_server, addr);
  if (port == 0) {
    PyErr_SetString(PyExc_RuntimeError, "Couldn't add port to server!");
    return NULL;
  }

  return PyInt_FromLong(port);
}

static PyObject *pygrpc_server_add_secure_http2_addr(Server *self,
                                                     PyObject *args) {
  const char *addr;
  int port;
  PyArg_ParseTuple(args, "s", &addr);
  port = grpc_server_add_secure_http2_port(self->c_server, addr);
  if (port == 0) {
    PyErr_SetString(PyExc_RuntimeError, "Couldn't add port to server!");
    return NULL;
  }
  return PyInt_FromLong(port);
}

static PyObject *pygrpc_server_start(Server *self) {
  grpc_server_start(self->c_server);

  Py_RETURN_NONE;
}

static const PyObject *pygrpc_server_service(Server *self, PyObject *args) {
  const PyObject *tag;
  grpc_call_error call_error;
  const PyObject *result;

  if (!(PyArg_ParseTuple(args, "O", &tag))) {
    return NULL;
  }

  call_error = grpc_server_request_call_old(self->c_server, (void *)tag);

  result = pygrpc_translate_call_error(call_error);
  if (result != NULL) {
    Py_INCREF(tag);
  }
  return result;
}

static PyObject *pygrpc_server_stop(Server *self) {
  grpc_server_shutdown(self->c_server);

  Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"add_http2_addr", (PyCFunction)pygrpc_server_add_http2_addr, METH_VARARGS,
     "Add an HTTP2 address."},
    {"add_secure_http2_addr", (PyCFunction)pygrpc_server_add_secure_http2_addr,
     METH_VARARGS, "Add a secure HTTP2 address."},
    {"start", (PyCFunction)pygrpc_server_start, METH_NOARGS,
     "Starts the server."},
    {"service", (PyCFunction)pygrpc_server_service, METH_VARARGS,
     "Services a call."},
    {"stop", (PyCFunction)pygrpc_server_stop, METH_NOARGS, "Stops the server."},
    {NULL}};

static PyTypeObject pygrpc_ServerType = {
    PyObject_HEAD_INIT(NULL)0,         /*ob_size*/
    "_gprc.Server",                    /*tp_name*/
    sizeof(Server),                    /*tp_basicsize*/
    0,                                 /*tp_itemsize*/
    (destructor)pygrpc_server_dealloc, /*tp_dealloc*/
    0,                                 /*tp_print*/
    0,                                 /*tp_getattr*/
    0,                                 /*tp_setattr*/
    0,                                 /*tp_compare*/
    0,                                 /*tp_repr*/
    0,                                 /*tp_as_number*/
    0,                                 /*tp_as_sequence*/
    0,                                 /*tp_as_mapping*/
    0,                                 /*tp_hash */
    0,                                 /*tp_call*/
    0,                                 /*tp_str*/
    0,                                 /*tp_getattro*/
    0,                                 /*tp_setattro*/
    0,                                 /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,                /*tp_flags*/
    "Wrapping of grpc_server.",        /* tp_doc */
    0,                                 /* tp_traverse */
    0,                                 /* tp_clear */
    0,                                 /* tp_richcompare */
    0,                                 /* tp_weaklistoffset */
    0,                                 /* tp_iter */
    0,                                 /* tp_iternext */
    methods,                           /* tp_methods */
    0,                                 /* tp_members */
    0,                                 /* tp_getset */
    0,                                 /* tp_base */
    0,                                 /* tp_dict */
    0,                                 /* tp_descr_get */
    0,                                 /* tp_descr_set */
    0,                                 /* tp_dictoffset */
    (initproc)pygrpc_server_init,      /* tp_init */
};

int pygrpc_add_server(PyObject *module) {
  pygrpc_ServerType.tp_new = PyType_GenericNew;
  if (PyType_Ready(&pygrpc_ServerType) < 0) {
    PyErr_SetString(PyExc_RuntimeError, "Error defining pygrpc_ServerType!");
    return -1;
  }
  if (PyModule_AddObject(module, "Server", (PyObject *)&pygrpc_ServerType) ==
      -1) {
    PyErr_SetString(PyExc_ImportError, "Couldn't add Server type to module!");
    return -1;
  }
  return 0;
}
