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

#include "grpc/_adapter/_call.h"

#include <math.h>
#include <Python.h>
#include <grpc/grpc.h>

#include "grpc/_adapter/_channel.h"
#include "grpc/_adapter/_completion_queue.h"
#include "grpc/_adapter/_error.h"

static int pygrpc_call_init(Call *self, PyObject *args, PyObject *kwds) {
  const PyObject *channel;
  const char *method;
  const char *host;
  double deadline;
  static char *kwlist[] = {"channel", "method", "host", "deadline", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!ssd:Call", kwlist,
                                   &pygrpc_ChannelType, &channel, &method,
                                   &host, &deadline)) {
    return -1;
  }

  /* TODO(nathaniel): Hoist the gpr_timespec <-> PyFloat arithmetic into its own
   * function with its own test coverage.
   */
  self->c_call = grpc_channel_create_call_old(
      ((Channel *)channel)->c_channel, method, host,
      gpr_time_from_nanos(deadline * GPR_NS_PER_SEC));

  return 0;
}

static void pygrpc_call_dealloc(Call *self) {
  if (self->c_call != NULL) {
    grpc_call_destroy(self->c_call);
  }
  self->ob_type->tp_free((PyObject *)self);
}

static const PyObject *pygrpc_call_invoke(Call *self, PyObject *args) {
  const PyObject *completion_queue;
  const PyObject *metadata_tag;
  const PyObject *finish_tag;
  grpc_call_error call_error;
  const PyObject *result;

  if (!(PyArg_ParseTuple(args, "O!OO:invoke", &pygrpc_CompletionQueueType,
                         &completion_queue, &metadata_tag, &finish_tag))) {
    return NULL;
  }

  call_error = grpc_call_invoke_old(
      self->c_call, ((CompletionQueue *)completion_queue)->c_completion_queue,
      (void *)metadata_tag, (void *)finish_tag, 0);

  result = pygrpc_translate_call_error(call_error);
  if (result != NULL) {
    Py_INCREF(metadata_tag);
    Py_INCREF(finish_tag);
  }
  return result;
}

static const PyObject *pygrpc_call_write(Call *self, PyObject *args) {
  const char *bytes;
  int length;
  const PyObject *tag;
  gpr_slice slice;
  grpc_byte_buffer *byte_buffer;
  grpc_call_error call_error;
  const PyObject *result;

  if (!(PyArg_ParseTuple(args, "s#O:write", &bytes, &length, &tag))) {
    return NULL;
  }

  slice = gpr_slice_from_copied_buffer(bytes, length);
  byte_buffer = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);

  call_error =
      grpc_call_start_write_old(self->c_call, byte_buffer, (void *)tag, 0);

  grpc_byte_buffer_destroy(byte_buffer);

  result = pygrpc_translate_call_error(call_error);
  if (result != NULL) {
    Py_INCREF(tag);
  }
  return result;
}

static const PyObject *pygrpc_call_complete(Call *self, PyObject *tag) {
  grpc_call_error call_error;
  const PyObject *result;

  call_error = grpc_call_writes_done_old(self->c_call, (void *)tag);

  result = pygrpc_translate_call_error(call_error);
  if (result != NULL) {
    Py_INCREF(tag);
  }
  return result;
}

static const PyObject *pygrpc_call_accept(Call *self, PyObject *args) {
  const PyObject *completion_queue;
  const PyObject *tag;
  grpc_call_error call_error;
  const PyObject *result;

  if (!(PyArg_ParseTuple(args, "O!O:accept", &pygrpc_CompletionQueueType,
                         &completion_queue, &tag))) {
    return NULL;
  }

  call_error = grpc_call_server_accept_old(
      self->c_call, ((CompletionQueue *)completion_queue)->c_completion_queue,
      (void *)tag);
  result = pygrpc_translate_call_error(call_error);

  if (result != NULL) {
    Py_INCREF(tag);
  }

  return result;
}

static const PyObject *pygrpc_call_premetadata(Call *self) {
  /* TODO(nathaniel): Metadata support. */
  return pygrpc_translate_call_error(
      grpc_call_server_end_initial_metadata_old(self->c_call, 0));
}

static const PyObject *pygrpc_call_read(Call *self, PyObject *tag) {
  grpc_call_error call_error;
  const PyObject *result;

  call_error = grpc_call_start_read_old(self->c_call, (void *)tag);

  result = pygrpc_translate_call_error(call_error);
  if (result != NULL) {
    Py_INCREF(tag);
  }
  return result;
}

static const PyObject *pygrpc_call_status(Call *self, PyObject *args) {
  PyObject *status;
  PyObject *code;
  PyObject *details;
  const PyObject *tag;
  grpc_status_code c_code;
  char *c_message;
  grpc_call_error call_error;
  const PyObject *result;

  if (!(PyArg_ParseTuple(args, "OO:status", &status, &tag))) {
    return NULL;
  }

  code = PyObject_GetAttrString(status, "code");
  if (code == NULL) {
    return NULL;
  }
  details = PyObject_GetAttrString(status, "details");
  if (details == NULL) {
    Py_DECREF(code);
    return NULL;
  }
  c_code = PyInt_AsLong(code);
  Py_DECREF(code);
  if (c_code == -1 && PyErr_Occurred()) {
    Py_DECREF(details);
    return NULL;
  }
  c_message = PyBytes_AsString(details);
  Py_DECREF(details);
  if (c_message == NULL) {
    return NULL;
  }

  call_error = grpc_call_start_write_status_old(self->c_call, c_code, c_message,
                                                (void *)tag);

  result = pygrpc_translate_call_error(call_error);
  if (result != NULL) {
    Py_INCREF(tag);
  }
  return result;
}

static const PyObject *pygrpc_call_cancel(Call *self) {
  return pygrpc_translate_call_error(grpc_call_cancel(self->c_call));
}

static PyMethodDef methods[] = {
    {"invoke", (PyCFunction)pygrpc_call_invoke, METH_VARARGS,
     "Invoke this call."},
    {"write", (PyCFunction)pygrpc_call_write, METH_VARARGS,
     "Write bytes to this call."},
    {"complete", (PyCFunction)pygrpc_call_complete, METH_O,
     "Complete writes to this call."},
    {"accept", (PyCFunction)pygrpc_call_accept, METH_VARARGS, "Accept an RPC."},
    {"premetadata", (PyCFunction)pygrpc_call_premetadata, METH_VARARGS,
     "Indicate the end of leading metadata in the response."},
    {"read", (PyCFunction)pygrpc_call_read, METH_O,
     "Read bytes from this call."},
    {"status", (PyCFunction)pygrpc_call_status, METH_VARARGS,
     "Report this call's status."},
    {"cancel", (PyCFunction)pygrpc_call_cancel, METH_NOARGS,
     "Cancel this call."},
    {NULL}};

PyTypeObject pygrpc_CallType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_grpc.Call",                    /*tp_name*/
    sizeof(Call),                    /*tp_basicsize*/
    0,                               /*tp_itemsize*/
    (destructor)pygrpc_call_dealloc, /*tp_dealloc*/
    0,                               /*tp_print*/
    0,                               /*tp_getattr*/
    0,                               /*tp_setattr*/
    0,                               /*tp_compare*/
    0,                               /*tp_repr*/
    0,                               /*tp_as_number*/
    0,                               /*tp_as_sequence*/
    0,                               /*tp_as_mapping*/
    0,                               /*tp_hash */
    0,                               /*tp_call*/
    0,                               /*tp_str*/
    0,                               /*tp_getattro*/
    0,                               /*tp_setattro*/
    0,                               /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,              /*tp_flags*/
    "Wrapping of grpc_call.",        /* tp_doc */
    0,                               /* tp_traverse */
    0,                               /* tp_clear */
    0,                               /* tp_richcompare */
    0,                               /* tp_weaklistoffset */
    0,                               /* tp_iter */
    0,                               /* tp_iternext */
    methods,                         /* tp_methods */
    0,                               /* tp_members */
    0,                               /* tp_getset */
    0,                               /* tp_base */
    0,                               /* tp_dict */
    0,                               /* tp_descr_get */
    0,                               /* tp_descr_set */
    0,                               /* tp_dictoffset */
    (initproc)pygrpc_call_init,      /* tp_init */
    0,                               /* tp_alloc */
    PyType_GenericNew,               /* tp_new */
};

int pygrpc_add_call(PyObject *module) {
  if (PyType_Ready(&pygrpc_CallType) < 0) {
    return -1;
  }
  if (PyModule_AddObject(module, "Call", (PyObject *)&pygrpc_CallType) == -1) {
    return -1;
  }
  return 0;
}
