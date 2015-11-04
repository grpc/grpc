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


PyMethodDef pygrpc_CallCredentials_methods[] = {
    {"composite", (PyCFunction)pygrpc_CallCredentials_composite,
     METH_CLASS|METH_KEYWORDS, ""},
    {"compute_engine", (PyCFunction)pygrpc_CallCredentials_compute_engine,
     METH_CLASS|METH_NOARGS, ""},
    {"jwt", (PyCFunction)pygrpc_CallCredentials_jwt,
     METH_CLASS|METH_KEYWORDS, ""},
    {"refresh_token", (PyCFunction)pygrpc_CallCredentials_refresh_token,
     METH_CLASS|METH_KEYWORDS, ""},
    {"iam", (PyCFunction)pygrpc_CallCredentials_iam,
     METH_CLASS|METH_KEYWORDS, ""},
    {NULL}
};

const char pygrpc_CallCredentials_doc[] = "";
PyTypeObject pygrpc_CallCredentials_type = {
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
    "CallCredentials",                        /* tp_name */
    sizeof(CallCredentials),                  /* tp_basicsize */
    0,                                        /* tp_itemsize */
    (destructor)pygrpc_CallCredentials_dealloc, /* tp_dealloc */
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
    pygrpc_CallCredentials_doc,             /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pygrpc_CallCredentials_methods,         /* tp_methods */
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

void pygrpc_CallCredentials_dealloc(CallCredentials *self) {
  grpc_call_credentials_release(self->c_creds);
  self->ob_type->tp_free((PyObject *)self);
}

CallCredentials *pygrpc_CallCredentials_composite(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  CallCredentials *self;
  CallCredentials *creds1;
  CallCredentials *creds2;
  static char *keywords[] = {"creds1", "creds2", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!:composite", keywords,
        &pygrpc_CallCredentials_type, &creds1,
        &pygrpc_CallCredentials_type, &creds2)) {
    return NULL;
  }
  self = (CallCredentials *)type->tp_alloc(type, 0);
  self->c_creds =
      grpc_composite_call_credentials_create(
          creds1->c_creds, creds2->c_creds, NULL);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create composite credentials");
    return NULL;
  }
  return self;
}

CallCredentials *pygrpc_CallCredentials_compute_engine(
    PyTypeObject *type, PyObject *ignored) {
  CallCredentials *self = (CallCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_google_compute_engine_credentials_create(NULL);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create compute engine credentials");
    return NULL;
  }
  return self;
}

/* TODO: Rename this credentials to something like service_account_jwt_access */
CallCredentials *pygrpc_CallCredentials_jwt(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  CallCredentials *self;
  const char *json_key;
  double lifetime;
  static char *keywords[] = {"json_key", "token_lifetime", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sd:jwt", keywords,
        &json_key, &lifetime)) {
    return NULL;
  }
  self = (CallCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_service_account_jwt_access_credentials_create(
      json_key, pygrpc_cast_double_to_gpr_timespec(lifetime), NULL);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create JWT credentials");
    return NULL;
  }
  return self;
}

CallCredentials *pygrpc_CallCredentials_refresh_token(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  CallCredentials *self;
  const char *json_refresh_token;
  static char *keywords[] = {"json_refresh_token", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s:refresh_token", keywords,
        &json_refresh_token)) {
    return NULL;
  }
  self = (CallCredentials *)type->tp_alloc(type, 0);
  self->c_creds =
      grpc_google_refresh_token_credentials_create(json_refresh_token, NULL);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't create credentials from refresh token");
    return NULL;
  }
  return self;
}

CallCredentials *pygrpc_CallCredentials_iam(
    PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  CallCredentials *self;
  const char *authorization_token;
  const char *authority_selector;
  static char *keywords[] = {"authorization_token", "authority_selector", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss:iam", keywords,
        &authorization_token, &authority_selector)) {
    return NULL;
  }
  self = (CallCredentials *)type->tp_alloc(type, 0);
  self->c_creds = grpc_google_iam_credentials_create(authorization_token,
                                                     authority_selector, NULL);
  if (!self->c_creds) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create IAM credentials");
    return NULL;
  }
  return self;
}

