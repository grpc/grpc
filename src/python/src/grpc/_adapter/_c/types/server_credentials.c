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
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>


PyMethodDef pygrpc_ServerCredentials_methods[] = {
    {"ssl", (PyCFunction)pygrpc_ServerCredentials_ssl,
     METH_CLASS|METH_KEYWORDS, ""},
    {"fake_transport_security",
     (PyCFunction)pygrpc_ServerCredentials_fake_transport_security,
     METH_CLASS|METH_NOARGS, ""},
    {NULL}
};
const char pygrpc_ServerCredentials_doc[] = "";
PyTypeObject pygrpc_ServerCredentials_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "ServerCredentials",                      /* tp_name */
    sizeof(ServerCredentials),                /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_ServerCredentials_dealloc, /* tp_dealloc */
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
    pygrpc_ServerCredentials_doc,             /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_ServerCredentials_methods,         /* tp_methods */
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

void pygrpc_ServerCredentials_dealloc(ServerCredentials *self) {
  grpc_server_credentials_release(self->c_creds);
  self->ob_type->tp_free((PyObject *)self);
}

ServerCredentials *pygrpc_ServerCredentials_ssl(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ServerCredentials *self;
  const char *root_certs;
  PyObject *py_key_cert_pairs;
  grpc_ssl_pem_key_cert_pair *key_cert_pairs;
  size_t num_key_cert_pairs;
  size_t i;
  static char *keywords[] = {"root_certs", "key_cert_pairs", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "zO:ssl", keywords,
        &root_certs, &py_key_cert_pairs)) {
    return NULL;
  }
  if (!PyList_Check(py_key_cert_pairs)) {
    PyErr_SetString(PyExc_TypeError, "expected a list of 2-tuples of strings");
    return NULL;
  }
  num_key_cert_pairs = PyList_Size(py_key_cert_pairs);
  key_cert_pairs =
      gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair) * num_key_cert_pairs);
  for (i = 0; i < num_key_cert_pairs; ++i) {
    PyObject *item = PyList_GET_ITEM(py_key_cert_pairs, i);
    const char *key;
    const char *cert;
    if (!PyArg_ParseTuple(item, "zz", &key, &cert)) {
      gpr_free(key_cert_pairs);
      PyErr_SetString(PyExc_TypeError,
                      "expected a list of 2-tuples of strings");
      return NULL;
    }
    key_cert_pairs[i].private_key = key;
    key_cert_pairs[i].cert_chain = cert;
  }

  self = (ServerCredentials *)type->tp_alloc(type, 0);
  /* TODO: Add a force_client_auth parameter in the python object and pass it
     here as the last arg. */
  self->c_creds = grpc_ssl_server_credentials_create(
      root_certs, key_cert_pairs, num_key_cert_pairs, 0);
  gpr_free(key_cert_pairs);
  return self;
}

ServerCredentials *pygrpc_ServerCredentials_fake_transport_security(
    PyTypeObject *type, PyObject *ignored) {
  ServerCredentials *self = (ServerCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_fake_transport_security_server_credentials_create();
  return self;
}

