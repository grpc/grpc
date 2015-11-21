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


PyMethodDef pygrpc_Call_methods[] = {
    {"start_batch", (PyCFunction)pygrpc_Call_start_batch, METH_KEYWORDS, ""},
    {"cancel", (PyCFunction)pygrpc_Call_cancel, METH_KEYWORDS, ""},
    {"peer", (PyCFunction)pygrpc_Call_peer, METH_NOARGS, ""},
    {"set_credentials", (PyCFunction)pygrpc_Call_set_credentials, METH_KEYWORDS,
     ""},
    {NULL}
};
const char pygrpc_Call_doc[] = "See grpc._adapter._types.Call.";
PyTypeObject pygrpc_Call_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "Call",                                   /* tp_name */
    sizeof(Call),                             /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_Call_dealloc,          /* tp_dealloc */
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
    pygrpc_Call_doc,                          /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_Call_methods,                      /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    0,                                        /* tp_alloc */
    0                                         /* tp_new */
};

Call *pygrpc_Call_new_empty(CompletionQueue *cq) {
  Call *call = (Call *)pygrpc_Call_type.tp_alloc(&pygrpc_Call_type, 0);
  call->c_call = NULL;
  call->cq = cq;
  Py_XINCREF(call->cq);
  return call;
}
void pygrpc_Call_dealloc(Call *self) {
  if (self->c_call) {
    grpc_call_destroy(self->c_call);
  }
  Py_XDECREF(self->cq);
  self->ob_type->tp_free((PyObject *)self);
}
PyObject *pygrpc_Call_start_batch(Call *self, PyObject *args, PyObject *kwargs) {
  PyObject *op_list;
  PyObject *user_tag;
  grpc_op *ops;
  size_t nops;
  size_t i;
  size_t j;
  pygrpc_tag *tag;
  grpc_call_error errcode;
  static char *keywords[] = {"ops", "tag", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO:start_batch", keywords,
                                   &op_list, &user_tag)) {
    return NULL;
  }
  if (!PyList_Check(op_list)) {
    PyErr_SetString(PyExc_TypeError, "expected a list of OpArgs");
    return NULL;
  }
  nops = PyList_Size(op_list);
  ops = gpr_malloc(sizeof(grpc_op) * nops);
  for (i = 0; i < nops; ++i) {
    PyObject *item = PyList_GET_ITEM(op_list, i);
    if (!pygrpc_produce_op(item, &ops[i])) {
      for (j = 0; j < i; ++j) {
        pygrpc_discard_op(ops[j]);
      }
      return NULL;
    }
  }
  tag = pygrpc_produce_batch_tag(user_tag, self, ops, nops);
  errcode = grpc_call_start_batch(self->c_call, tag->ops, tag->nops, tag, NULL);
  gpr_free(ops);
  return PyInt_FromLong(errcode);
}
PyObject *pygrpc_Call_cancel(Call *self, PyObject *args, PyObject *kwargs) {
  PyObject *py_code = NULL;
  grpc_call_error errcode;
  int code;
  char *details = NULL;
  static char *keywords[] = {"code", "details", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Os:start_batch", keywords,
                                   &py_code, &details)) {
    return NULL;
  }
  if (py_code != NULL && details != NULL) {
    if (!PyInt_Check(py_code)) {
      PyErr_SetString(PyExc_TypeError, "expected integer code");
      return NULL;
    }
    code = PyInt_AsLong(py_code);
    errcode = grpc_call_cancel_with_status(self->c_call, code, details, NULL);
  } else if (py_code != NULL || details != NULL) {
    PyErr_SetString(PyExc_ValueError,
                    "if `code` is specified, so must `details`");
    return NULL;
  } else {
    errcode = grpc_call_cancel(self->c_call, NULL);
  }
  return PyInt_FromLong(errcode);
}

PyObject *pygrpc_Call_peer(Call *self) {
  char *peer = grpc_call_get_peer(self->c_call);
  PyObject *py_peer = PyString_FromString(peer);
  gpr_free(peer);
  return py_peer;
}
PyObject *pygrpc_Call_set_credentials(Call *self, PyObject *args,
                                      PyObject *kwargs) {
  CallCredentials *creds;
  grpc_call_error errcode;
  static char *keywords[] = {"creds", NULL};
  if (!PyArg_ParseTupleAndKeywords(
      args, kwargs, "O!:set_credentials", keywords,
      &pygrpc_CallCredentials_type, &creds)) {
    return NULL;
  }
  errcode = grpc_call_set_credentials(self->c_call, creds->c_creds);
  return PyInt_FromLong(errcode);
}
