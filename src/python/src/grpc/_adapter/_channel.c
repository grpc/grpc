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

#include "grpc/_adapter/_channel.h"

#include <Python.h>
#include <grpc/grpc.h>

static int pygrpc_channel_init(Channel *self, PyObject *args, PyObject *kwds) {
  const char *hostport;

  if (!(PyArg_ParseTuple(args, "s", &hostport))) {
    self->c_channel = NULL;
    return -1;
  }

  self->c_channel = grpc_channel_create(hostport, NULL);
  return 0;
}

static void pygrpc_channel_dealloc(Channel *self) {
  if (self->c_channel != NULL) {
    grpc_channel_destroy(self->c_channel);
  }
  self->ob_type->tp_free((PyObject *)self);
}

PyTypeObject pygrpc_ChannelType = {
    PyObject_HEAD_INIT(NULL)0,          /*ob_size*/
    "_grpc.Channel",                    /*tp_name*/
    sizeof(Channel),                    /*tp_basicsize*/
    0,                                  /*tp_itemsize*/
    (destructor)pygrpc_channel_dealloc, /*tp_dealloc*/
    0,                                  /*tp_print*/
    0,                                  /*tp_getattr*/
    0,                                  /*tp_setattr*/
    0,                                  /*tp_compare*/
    0,                                  /*tp_repr*/
    0,                                  /*tp_as_number*/
    0,                                  /*tp_as_sequence*/
    0,                                  /*tp_as_mapping*/
    0,                                  /*tp_hash */
    0,                                  /*tp_call*/
    0,                                  /*tp_str*/
    0,                                  /*tp_getattro*/
    0,                                  /*tp_setattro*/
    0,                                  /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,                 /*tp_flags*/
    "Wrapping of grpc_channel.",        /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    0,                                  /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    (initproc)pygrpc_channel_init,      /* tp_init */
};

int pygrpc_add_channel(PyObject *module) {
  pygrpc_ChannelType.tp_new = PyType_GenericNew;
  if (PyType_Ready(&pygrpc_ChannelType) < 0) {
    PyErr_SetString(PyExc_RuntimeError, "Error defining pygrpc_ChannelType!");
    return -1;
  }
  if (PyModule_AddObject(module, "Channel", (PyObject *)&pygrpc_ChannelType) ==
      -1) {
    PyErr_SetString(PyExc_ImportError, "Couldn't add Channel type to module!");
    return -1;
  }
  return 0;
}
