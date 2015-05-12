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
#include <grpc/support/alloc.h>

#include "grpc/_adapter/_channel.h"
#include "grpc/_adapter/_completion_queue.h"
#include "grpc/_adapter/_error.h"
#include "grpc/_adapter/_tag.h"

static PyObject *pygrpc_call_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  Call *self = (Call *)type->tp_alloc(type, 0);
  Channel *channel;
  CompletionQueue *completion_queue;
  const char *method;
  const char *host;
  double deadline;
  static char *kwlist[] = {"channel", "completion_queue",
    "method", "host", "deadline", NULL};

  if (!PyArg_ParseTupleAndKeywords(
      args, kwds, "O!O!ssd:Call", kwlist,
      &pygrpc_ChannelType, &channel,
      &pygrpc_CompletionQueueType, &completion_queue,
      &method, &host, &deadline)) {
    return NULL;
  }

  /* TODO(nathaniel): Hoist the gpr_timespec <-> PyFloat arithmetic into its own
   * function with its own test coverage.
   */
  self->c_call = grpc_channel_create_call(
      channel->c_channel, completion_queue->c_completion_queue, method, host,
      gpr_time_from_nanos(deadline * GPR_NS_PER_SEC));
  self->completion_queue = completion_queue;
  Py_INCREF(self->completion_queue);
  self->channel = channel;
  Py_INCREF(self->channel);
  grpc_call_details_init(&self->call_details);
  grpc_metadata_array_init(&self->recv_metadata);
  grpc_metadata_array_init(&self->recv_trailing_metadata);
  self->send_metadata = NULL;
  self->send_metadata_count = 0;
  self->send_trailing_metadata = NULL;
  self->send_trailing_metadata_count = 0;
  self->send_message = NULL;
  self->recv_message = NULL;
  self->adding_to_trailing = 0;

  return (PyObject *)self;
}

static void pygrpc_call_dealloc(Call *self) {
  if (self->c_call != NULL) {
    grpc_call_destroy(self->c_call);
  }
  Py_XDECREF(self->completion_queue);
  Py_XDECREF(self->channel);
  Py_XDECREF(self->server);
  grpc_call_details_destroy(&self->call_details);
  grpc_metadata_array_destroy(&self->recv_metadata);
  grpc_metadata_array_destroy(&self->recv_trailing_metadata);
  if (self->send_message) {
    grpc_byte_buffer_destroy(self->send_message);
  }
  if (self->recv_message) {
    grpc_byte_buffer_destroy(self->recv_message);
  }
  gpr_free(self->status_details);
  gpr_free(self->send_metadata);
  gpr_free(self->send_trailing_metadata);
  self->ob_type->tp_free((PyObject *)self);
}

static const PyObject *pygrpc_call_invoke(Call *self, PyObject *args) {
  PyObject *completion_queue;
  PyObject *metadata_tag;
  PyObject *finish_tag;
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_init_metadata_tag;
  pygrpc_tag *c_metadata_tag;
  pygrpc_tag *c_finish_tag;
  grpc_op send_initial_metadata;
  grpc_op recv_initial_metadata;
  grpc_op recv_status_on_client;

  if (!(PyArg_ParseTuple(args, "O!OO:invoke", &pygrpc_CompletionQueueType,
                         &completion_queue, &metadata_tag, &finish_tag))) {
    return NULL;
  }
  send_initial_metadata.op = GRPC_OP_SEND_INITIAL_METADATA;
  send_initial_metadata.data.send_initial_metadata.metadata = self->send_metadata;
  send_initial_metadata.data.send_initial_metadata.count = self->send_metadata_count;
  recv_initial_metadata.op = GRPC_OP_RECV_INITIAL_METADATA;
  recv_initial_metadata.data.recv_initial_metadata = &self->recv_metadata;
  recv_status_on_client.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  recv_status_on_client.data.recv_status_on_client.trailing_metadata = &self->recv_trailing_metadata;
  recv_status_on_client.data.recv_status_on_client.status = &self->status;
  recv_status_on_client.data.recv_status_on_client.status_details = &self->status_details;
  recv_status_on_client.data.recv_status_on_client.status_details_capacity = &self->status_details_capacity;
  c_init_metadata_tag = pygrpc_tag_new(PYGRPC_INITIAL_METADATA, NULL, self);
  c_metadata_tag = pygrpc_tag_new(PYGRPC_CLIENT_METADATA_READ, metadata_tag, self);
  c_finish_tag = pygrpc_tag_new(PYGRPC_FINISHED_CLIENT, finish_tag, self);

  call_error = grpc_call_start_batch(self->c_call, &send_initial_metadata, 1, c_init_metadata_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_init_metadata_tag);
    pygrpc_tag_destroy(c_metadata_tag);
    pygrpc_tag_destroy(c_finish_tag);
    return result;
  }
  call_error = grpc_call_start_batch(self->c_call, &recv_initial_metadata, 1, c_metadata_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_metadata_tag);
    pygrpc_tag_destroy(c_finish_tag);
    return result;
  }
  call_error = grpc_call_start_batch(self->c_call, &recv_status_on_client, 1, c_finish_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_finish_tag);
    return result;
  }

  return result;
}

static const PyObject *pygrpc_call_write(Call *self, PyObject *args) {
  const char *bytes;
  int length;
  PyObject *tag;
  gpr_slice slice;
  grpc_byte_buffer *byte_buffer;
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_tag;
  grpc_op op;

  if (!(PyArg_ParseTuple(args, "s#O:write", &bytes, &length, &tag))) {
    return NULL;
  }
  c_tag = pygrpc_tag_new(PYGRPC_WRITE_ACCEPTED, tag, self);

  slice = gpr_slice_from_copied_buffer(bytes, length);
  byte_buffer = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);

  if (self->send_message) {
    grpc_byte_buffer_destroy(self->send_message);
  }
  self->send_message = byte_buffer;

  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message = self->send_message;

  call_error = grpc_call_start_batch(self->c_call, &op, 1, c_tag);

  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_tag);
  }
  return result;
}

static const PyObject *pygrpc_call_complete(Call *self, PyObject *tag) {
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_tag = pygrpc_tag_new(PYGRPC_FINISH_ACCEPTED, tag, self);
  grpc_op op;

  op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;

  call_error = grpc_call_start_batch(self->c_call, &op, 1, c_tag);

  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_tag);
  }
  return result;
}

static const PyObject *pygrpc_call_accept(Call *self, PyObject *args) {
  PyObject *completion_queue;
  PyObject *tag;
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_tag;
  grpc_op op;

  if (!(PyArg_ParseTuple(args, "O!O:accept", &pygrpc_CompletionQueueType,
                         &completion_queue, &tag))) {
    return NULL;
  }

  op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op.data.recv_close_on_server.cancelled = &self->cancelled;
  c_tag = pygrpc_tag_new(PYGRPC_FINISHED_SERVER, tag, self);

  call_error = grpc_call_start_batch(self->c_call, &op, 1, c_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_tag);
  }
  return result;
}

static const PyObject *pygrpc_call_add_metadata(Call *self, PyObject *args) {
  const char* key = NULL;
  const char* value = NULL;
  int value_length = 0;
  grpc_metadata metadata;
  if (!PyArg_ParseTuple(args, "ss#", &key, &value, &value_length)) {
    return NULL;
  }
  metadata.key = key;
  metadata.value = value;
  metadata.value_length = value_length;
  if (self->adding_to_trailing) {
    self->send_trailing_metadata = gpr_realloc(self->send_trailing_metadata, (self->send_trailing_metadata_count + 1) * sizeof(grpc_metadata));
    self->send_trailing_metadata[self->send_trailing_metadata_count] = metadata;
    self->send_trailing_metadata_count = self->send_trailing_metadata_count + 1;
  } else {
    self->send_metadata = gpr_realloc(self->send_metadata, (self->send_metadata_count + 1) * sizeof(grpc_metadata));
    self->send_metadata[self->send_metadata_count] = metadata;
    self->send_metadata_count = self->send_metadata_count + 1;
  }
  return pygrpc_translate_call_error(GRPC_CALL_OK);
}

static const PyObject *pygrpc_call_premetadata(Call *self) {
  grpc_op op;
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_tag = pygrpc_tag_new(PYGRPC_INITIAL_METADATA, NULL, self);
  op.op = GRPC_OP_SEND_INITIAL_METADATA;
  op.data.send_initial_metadata.metadata = self->send_metadata;
  op.data.send_initial_metadata.count = self->send_metadata_count;
  self->adding_to_trailing = 1;

  call_error = grpc_call_start_batch(self->c_call, &op, 1, c_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_tag);
  }
  return result;
}

static const PyObject *pygrpc_call_read(Call *self, PyObject *tag) {
  grpc_op op;
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_tag = pygrpc_tag_new(PYGRPC_READ, tag, self);

  op.op = GRPC_OP_RECV_MESSAGE;
  if (self->recv_message) {
    grpc_byte_buffer_destroy(self->recv_message);
    self->recv_message = NULL;
  }
  op.data.recv_message = &self->recv_message;
  call_error = grpc_call_start_batch(self->c_call, &op, 1, c_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_tag);
  }
  return result;
}

static const PyObject *pygrpc_call_status(Call *self, PyObject *args) {
  PyObject *status;
  PyObject *code;
  PyObject *details;
  PyObject *tag;
  grpc_status_code c_code;
  char *c_message;
  grpc_call_error call_error;
  const PyObject *result;
  pygrpc_tag *c_tag;
  grpc_op op;

  if (!(PyArg_ParseTuple(args, "OO:status", &status, &tag))) {
    return NULL;
  }
  c_tag = pygrpc_tag_new(PYGRPC_FINISH_ACCEPTED, tag, self);

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
  if (self->status_details) {
    gpr_free(self->status_details);
  }
  self->status_details = gpr_malloc(strlen(c_message)+1);
  strcpy(self->status_details, c_message);
  op.op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op.data.send_status_from_server.trailing_metadata_count = self->send_trailing_metadata_count;
  op.data.send_status_from_server.trailing_metadata = self->send_trailing_metadata;
  op.data.send_status_from_server.status = c_code;
  op.data.send_status_from_server.status_details = self->status_details;

  call_error = grpc_call_start_batch(self->c_call, &op, 1, c_tag);
  result = pygrpc_translate_call_error(call_error);
  if (result == NULL) {
    pygrpc_tag_destroy(c_tag);
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
    {"add_metadata", (PyCFunction)pygrpc_call_add_metadata, METH_VARARGS,
     "Add metadata to the call. May not be called after invoke on the client "
     "side. On the server side: when called before premetadata it provides "
     "'leading' metadata, when called after premetadata but before status it "
     "provides 'trailing metadata'; may not be called after status."},
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
    0,                               /* tp_init */
    0,                               /* tp_alloc */
    pygrpc_call_new,                 /* tp_new */
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
