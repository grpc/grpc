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

#include "grpc/_adapter/_client_credentials.h"

#include <Python.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>

static int pygrpc_client_credentials_init(ClientCredentials *self,
                                          PyObject *args, PyObject *kwds) {
  char *root_certificates;
  grpc_ssl_pem_key_cert_pair key_certificate_pair;
  static char *kwlist[] = {"root_certificates", "private_key",
                           "certificate_chain", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "zzz:ClientCredentials", kwlist,
                                   &root_certificates,
                                   &key_certificate_pair.private_key,
                                   &key_certificate_pair.cert_chain)) {
    return -1;
  }

  if (key_certificate_pair.private_key != NULL && key_certificate_pair.cert_chain != NULL) {
    self->c_client_credentials =
        grpc_ssl_credentials_create(root_certificates, &key_certificate_pair);
  } else {
    self->c_client_credentials =
        grpc_ssl_credentials_create(root_certificates, NULL);
  }
  return 0;
}

static void pygrpc_client_credentials_dealloc(ClientCredentials *self) {
  if (self->c_client_credentials != NULL) {
    grpc_credentials_release(self->c_client_credentials);
  }
  self->ob_type->tp_free((PyObject *)self);
}

PyTypeObject pygrpc_ClientCredentialsType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_grpc.ClientCredencials",                      /*tp_name*/
    sizeof(ClientCredentials),                      /*tp_basicsize*/
    0,                                              /*tp_itemsize*/
    (destructor)pygrpc_client_credentials_dealloc,  /*tp_dealloc*/
    0,                                              /*tp_print*/
    0,                                              /*tp_getattr*/
    0,                                              /*tp_setattr*/
    0,                                              /*tp_compare*/
    0,                                              /*tp_repr*/
    0,                                              /*tp_as_number*/
    0,                                              /*tp_as_sequence*/
    0,                                              /*tp_as_mapping*/
    0,                                              /*tp_hash */
    0,                                              /*tp_call*/
    0,                                              /*tp_str*/
    0,                                              /*tp_getattro*/
    0,                                              /*tp_setattro*/
    0,                                              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,                             /*tp_flags*/
    "Wrapping of grpc_credentials.",                /* tp_doc */
    0,                                              /* tp_traverse */
    0,                                              /* tp_clear */
    0,                                              /* tp_richcompare */
    0,                                              /* tp_weaklistoffset */
    0,                                              /* tp_iter */
    0,                                              /* tp_iternext */
    0,                                              /* tp_methods */
    0,                                              /* tp_members */
    0,                                              /* tp_getset */
    0,                                              /* tp_base */
    0,                                              /* tp_dict */
    0,                                              /* tp_descr_get */
    0,                                              /* tp_descr_set */
    0,                                              /* tp_dictoffset */
    (initproc)pygrpc_client_credentials_init,       /* tp_init */
    0,                                              /* tp_alloc */
    PyType_GenericNew,                              /* tp_new */
};

int pygrpc_add_client_credentials(PyObject *module) {
  if (PyType_Ready(&pygrpc_ClientCredentialsType) < 0) {
    return -1;
  }
  if (PyModule_AddObject(module, "ClientCredentials",
                         (PyObject *)&pygrpc_ClientCredentialsType) == -1) {
    return -1;
  }
  return 0;
}
