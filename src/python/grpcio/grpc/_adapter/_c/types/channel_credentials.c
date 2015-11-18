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


PyMethodDef pygrpc_ChannelCredentials_methods[] = {
    {"google_default", (PyCFunction)pygrpc_ChannelCredentials_google_default,
     METH_CLASS|METH_NOARGS, ""},
    {"ssl", (PyCFunction)pygrpc_ChannelCredentials_ssl,
     METH_CLASS|METH_KEYWORDS, ""},
    {"composite", (PyCFunction)pygrpc_ChannelCredentials_composite,
     METH_CLASS|METH_KEYWORDS, ""},
    {NULL}
};

const char pygrpc_ChannelCredentials_doc[] = "";
PyTypeObject pygrpc_ChannelCredentials_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "ChannelCredentials",                     /* tp_name */
    sizeof(ChannelCredentials),               /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_ChannelCredentials_dealloc, /* tp_dealloc */
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
    pygrpc_ChannelCredentials_doc,             /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_ChannelCredentials_methods,         /* tp_methods */
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

void pygrpc_ChannelCredentials_dealloc(ChannelCredentials *self) {
  grpc_channel_credentials_release(self->c_creds);
  self->ob_type->tp_free((PyObject *)self);
}

ChannelCredentials *pygrpc_ChannelCredentials_google_default(
    PyTypeObject *type, PyObject *ignored) {
  ChannelCredentials *self = (ChannelCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_google_default_credentials_create();
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create Google default credentials");
    return NULL;
  }
  return self;
}

ChannelCredentials *pygrpc_ChannelCredentials_ssl(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ChannelCredentials *self;
  const char *root_certs;
  const char *private_key = NULL;
  const char *cert_chain = NULL;
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  static char *keywords[] = {"root_certs", "private_key", "cert_chain", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "z|zz:ssl", keywords,
        &root_certs, &private_key, &cert_chain)) {
    return NULL;
  }
  self = (ChannelCredentials *)type->tp_alloc(type, 0);
  if (private_key && cert_chain) {
    key_cert_pair.private_key = private_key;
    key_cert_pair.cert_chain = cert_chain;
    self->c_creds =
        grpc_ssl_credentials_create(root_certs, &key_cert_pair, NULL);
  } else {
    self->c_creds = grpc_ssl_credentials_create(root_certs, NULL, NULL);
  }
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create ssl credentials");
    return NULL;
  }
  return self;
}

ChannelCredentials *pygrpc_ChannelCredentials_composite(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ChannelCredentials *self;
  ChannelCredentials *creds1;
  CallCredentials *creds2;
  static char *keywords[] = {"creds1", "creds2", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!:composite", keywords,
        &pygrpc_ChannelCredentials_type, &creds1,
        &pygrpc_CallCredentials_type, &creds2)) {
    return NULL;
  }
  self = (ChannelCredentials *)type->tp_alloc(type, 0);
  self->c_creds =
      grpc_composite_channel_credentials_create(
          creds1->c_creds, creds2->c_creds, NULL);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(
        PyExc_RuntimeError, "couldn't create composite credentials");
    return NULL;
  }
  return self;
}

