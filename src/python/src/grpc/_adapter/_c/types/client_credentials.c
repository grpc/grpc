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


PyMethodDef pygrpc_ClientCredentials_methods[] = {
    {"google_default", (PyCFunction)pygrpc_ClientCredentials_google_default,
     METH_CLASS|METH_NOARGS, ""},
    {"ssl", (PyCFunction)pygrpc_ClientCredentials_ssl,
     METH_CLASS|METH_KEYWORDS, ""},
    {"composite", (PyCFunction)pygrpc_ClientCredentials_composite,
     METH_CLASS|METH_KEYWORDS, ""},
    {"compute_engine", (PyCFunction)pygrpc_ClientCredentials_compute_engine,
     METH_CLASS|METH_NOARGS, ""},
    {"service_account", (PyCFunction)pygrpc_ClientCredentials_service_account,
     METH_CLASS|METH_KEYWORDS, ""},
    {"jwt", (PyCFunction)pygrpc_ClientCredentials_jwt,
     METH_CLASS|METH_KEYWORDS, ""},
    {"refresh_token", (PyCFunction)pygrpc_ClientCredentials_refresh_token,
     METH_CLASS|METH_KEYWORDS, ""},
    {"iam", (PyCFunction)pygrpc_ClientCredentials_iam,
     METH_CLASS|METH_KEYWORDS, ""},
    {NULL}
};
const char pygrpc_ClientCredentials_doc[] = "";
PyTypeObject pygrpc_ClientCredentials_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "ClientCredentials",                      /* tp_name */
    sizeof(ClientCredentials),                /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_ClientCredentials_dealloc, /* tp_dealloc */
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
    pygrpc_ClientCredentials_doc,             /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_ClientCredentials_methods,         /* tp_methods */
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

void pygrpc_ClientCredentials_dealloc(ClientCredentials *self) {
  grpc_credentials_release(self->c_creds);
  self->ob_type->tp_free((PyObject *)self);
}

ClientCredentials *pygrpc_ClientCredentials_google_default(
    PyTypeObject *type, PyObject *ignored) {
  ClientCredentials *self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_google_default_credentials_create();
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create Google default credentials");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_ssl(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ClientCredentials *self;
  const char *root_certs;
  const char *private_key = NULL;
  const char *cert_chain = NULL;
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  static char *keywords[] = {"root_certs", "private_key", "cert_chain", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "z|zz:ssl", keywords,
        &root_certs, &private_key, &cert_chain)) {
    return NULL;
  }
  self = (ClientCredentials *)type->tp_alloc(type, 0);
  if (private_key && cert_chain) {
    key_cert_pair.private_key = private_key;
    key_cert_pair.cert_chain = cert_chain;
    self->c_creds = grpc_ssl_credentials_create(root_certs, &key_cert_pair);
  } else {
    self->c_creds = grpc_ssl_credentials_create(root_certs, NULL);
  }
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create ssl credentials");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_composite(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ClientCredentials *self;
  ClientCredentials *creds1;
  ClientCredentials *creds2;
  static char *keywords[] = {"creds1", "creds2", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!:composite", keywords,
        &pygrpc_ClientCredentials_type, &creds1,
        &pygrpc_ClientCredentials_type, &creds2)) {
    return NULL;
  }
  self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_composite_credentials_create(
      creds1->c_creds, creds2->c_creds);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create composite credentials");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_compute_engine(
    PyTypeObject *type, PyObject *ignored) {
  ClientCredentials *self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_compute_engine_credentials_create();
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create compute engine credentials");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_service_account(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ClientCredentials *self;
  const char *json_key;
  const char *scope;
  double lifetime;
  static char *keywords[] = {"json_key", "scope", "token_lifetime", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ssd:service_account", keywords,
        &json_key, &scope, &lifetime)) {
    return NULL;
  }
  self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_service_account_credentials_create(
      json_key, scope, pygrpc_cast_double_to_gpr_timespec(lifetime));
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create service account credentials");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_jwt(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ClientCredentials *self;
  const char *json_key;
  double lifetime;
  static char *keywords[] = {"json_key", "token_lifetime", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sd:jwt", keywords,
        &json_key, &lifetime)) {
    return NULL;
  }
  self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_jwt_credentials_create(
      json_key, pygrpc_cast_double_to_gpr_timespec(lifetime));
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create JWT credentials");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_refresh_token(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ClientCredentials *self;
  const char *json_refresh_token;
  static char *keywords[] = {"json_refresh_token", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s:refresh_token", keywords,
        &json_refresh_token)) {
    return NULL;
  }
  self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_refresh_token_credentials_create(json_refresh_token);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create credentials from refresh token");
    return NULL;
  }
  return self;
}

ClientCredentials *pygrpc_ClientCredentials_iam(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  ClientCredentials *self;
  const char *authorization_token;
  const char *authority_selector;
  static char *keywords[] = {"authorization_token", "authority_selector", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss:iam", keywords,
        &authorization_token, &authority_selector)) {
    return NULL;
  }
  self = (ClientCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_iam_credentials_create(authorization_token,
                                              authority_selector);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create IAM credentials");
    return NULL;
  }
  return self;
}

