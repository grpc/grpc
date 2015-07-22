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

#include <math.h>
#include <string.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <grpc/grpc.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/support/alloc.h>
#include <grpc/support/slice.h>
#include <grpc/support/time.h>
#include <grpc/support/string_util.h>

#include "grpc/_adapter/_c/types.h"

pygrpc_tag *pygrpc_produce_batch_tag(
    PyObject *user_tag, Call *call, grpc_op *ops, size_t nops) {
  pygrpc_tag *tag = gpr_malloc(sizeof(pygrpc_tag));
  tag->user_tag = user_tag;
  Py_XINCREF(tag->user_tag);
  tag->call = call;
  Py_XINCREF(tag->call);
  tag->ops = gpr_malloc(sizeof(grpc_op)*nops);
  memcpy(tag->ops, ops, sizeof(grpc_op)*nops);
  tag->nops = nops;
  grpc_call_details_init(&tag->request_call_details);
  grpc_metadata_array_init(&tag->request_metadata);
  tag->is_new_call = 0;
  return tag;
}

pygrpc_tag *pygrpc_produce_request_tag(PyObject *user_tag, Call *empty_call) {
  pygrpc_tag *tag = gpr_malloc(sizeof(pygrpc_tag));
  tag->user_tag = user_tag;
  Py_XINCREF(tag->user_tag);
  tag->call = empty_call;
  Py_XINCREF(tag->call);
  tag->ops = NULL;
  tag->nops = 0;
  grpc_call_details_init(&tag->request_call_details);
  grpc_metadata_array_init(&tag->request_metadata);
  tag->is_new_call = 1;
  return tag;
}

pygrpc_tag *pygrpc_produce_server_shutdown_tag(PyObject *user_tag) {
  pygrpc_tag *tag = gpr_malloc(sizeof(pygrpc_tag));
  tag->user_tag = user_tag;
  Py_XINCREF(tag->user_tag);
  tag->call = NULL;
  tag->ops = NULL;
  tag->nops = 0;
  grpc_call_details_init(&tag->request_call_details);
  grpc_metadata_array_init(&tag->request_metadata);
  tag->is_new_call = 0;
  return tag;
}

void pygrpc_discard_tag(pygrpc_tag *tag) {
  if (!tag) {
    return;
  }
  Py_XDECREF(tag->user_tag);
  Py_XDECREF(tag->call);
  gpr_free(tag->ops);
  grpc_call_details_destroy(&tag->request_call_details);
  grpc_metadata_array_destroy(&tag->request_metadata);
  gpr_free(tag);
}

PyObject *pygrpc_consume_event(grpc_event event) {
  pygrpc_tag *tag;
  PyObject *result;
  if (event.type == GRPC_QUEUE_TIMEOUT) {
    Py_RETURN_NONE;
  }
  tag = event.tag;
  switch (event.type) {
  case GRPC_QUEUE_SHUTDOWN:
    result = Py_BuildValue("iOOOOO", GRPC_QUEUE_SHUTDOWN,
                           Py_None, Py_None, Py_None, Py_None, Py_True);
    break;
  case GRPC_OP_COMPLETE:
    if (tag->is_new_call) {
      result = Py_BuildValue(
          "iOO(ssd)[(iNOOOO)]O", GRPC_OP_COMPLETE, tag->user_tag, tag->call,
          tag->request_call_details.method, tag->request_call_details.host,
          pygrpc_cast_gpr_timespec_to_double(tag->request_call_details.deadline),
          GRPC_OP_RECV_INITIAL_METADATA,
          pygrpc_cast_metadata_array_to_pyseq(tag->request_metadata), Py_None,
          Py_None, Py_None, Py_None,
          event.success ? Py_True : Py_False);
    } else {
      result = Py_BuildValue("iOOONO", GRPC_OP_COMPLETE, tag->user_tag,
          tag->call ? (PyObject*)tag->call : Py_None, Py_None,
          pygrpc_consume_ops(tag->ops, tag->nops),
          event.success ? Py_True : Py_False);
    }
    break;
  default:
    PyErr_SetString(PyExc_ValueError,
                    "unknown completion type; could not translate event");
    return NULL;
  }
  pygrpc_discard_tag(tag);
  return result;
}

int pygrpc_produce_op(PyObject *op, grpc_op *result) {
  static const int OP_TUPLE_SIZE = 5;
  static const int STATUS_TUPLE_SIZE = 2;
  static const int TYPE_INDEX = 0;
  static const int INITIAL_METADATA_INDEX = 1;
  static const int TRAILING_METADATA_INDEX = 2;
  static const int MESSAGE_INDEX = 3;
  static const int STATUS_INDEX = 4;
  static const int STATUS_CODE_INDEX = 0;
  static const int STATUS_DETAILS_INDEX = 1;
  int type;
  Py_ssize_t message_size;
  char *message;
  char *status_details;
  gpr_slice message_slice;
  grpc_op c_op;
  if (!PyTuple_Check(op)) {
    PyErr_SetString(PyExc_TypeError, "expected tuple op");
    return 0;
  }
  if (PyTuple_Size(op) != OP_TUPLE_SIZE) {
    char *buf;
    gpr_asprintf(&buf, "expected tuple op of length %d", OP_TUPLE_SIZE);
    PyErr_SetString(PyExc_ValueError, buf);
    gpr_free(buf);
    return 0;
  }
  type = PyInt_AsLong(PyTuple_GET_ITEM(op, TYPE_INDEX));
  if (PyErr_Occurred()) {
    return 0;
  }
  c_op.op = type;
  c_op.flags = 0;
  switch (type) {
  case GRPC_OP_SEND_INITIAL_METADATA:
    if (!pygrpc_cast_pyseq_to_send_metadata(
            PyTuple_GetItem(op, INITIAL_METADATA_INDEX),
            &c_op.data.send_initial_metadata.metadata,
            &c_op.data.send_initial_metadata.count)) {
      return 0;
    }
    break;
  case GRPC_OP_SEND_MESSAGE:
    PyString_AsStringAndSize(
        PyTuple_GET_ITEM(op, MESSAGE_INDEX), &message, &message_size);
    message_slice = gpr_slice_from_copied_buffer(message, message_size);
    c_op.data.send_message = grpc_raw_byte_buffer_create(&message_slice, 1);
    gpr_slice_unref(message_slice);
    break;
  case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
    /* Don't need to fill in any other fields. */
    break;
  case GRPC_OP_SEND_STATUS_FROM_SERVER:
    if (!pygrpc_cast_pyseq_to_send_metadata(
            PyTuple_GetItem(op, TRAILING_METADATA_INDEX),
            &c_op.data.send_status_from_server.trailing_metadata,
            &c_op.data.send_status_from_server.trailing_metadata_count)) {
      return 0;
    }
    if (!PyTuple_Check(PyTuple_GET_ITEM(op, STATUS_INDEX))) {
      char *buf;
      gpr_asprintf(&buf, "expected tuple status in op of length %d",
                   STATUS_TUPLE_SIZE);
      PyErr_SetString(PyExc_ValueError, buf);
      gpr_free(buf);
      return 0;
    }
    c_op.data.send_status_from_server.status = PyInt_AsLong(
        PyTuple_GET_ITEM(PyTuple_GET_ITEM(op, STATUS_INDEX), STATUS_CODE_INDEX));
    status_details = PyString_AsString(
        PyTuple_GET_ITEM(PyTuple_GET_ITEM(op, STATUS_INDEX), STATUS_DETAILS_INDEX));
    if (PyErr_Occurred()) {
      return 0;
    }
    c_op.data.send_status_from_server.status_details =
        gpr_malloc(strlen(status_details) + 1);
    strcpy((char *)c_op.data.send_status_from_server.status_details,
           status_details);
    break;
  case GRPC_OP_RECV_INITIAL_METADATA:
    c_op.data.recv_initial_metadata = gpr_malloc(sizeof(grpc_metadata_array));
    grpc_metadata_array_init(c_op.data.recv_initial_metadata);
    break;
  case GRPC_OP_RECV_MESSAGE:
    c_op.data.recv_message = gpr_malloc(sizeof(grpc_byte_buffer *));
    break;
  case GRPC_OP_RECV_STATUS_ON_CLIENT:
    c_op.data.recv_status_on_client.trailing_metadata =
        gpr_malloc(sizeof(grpc_metadata_array));
    grpc_metadata_array_init(c_op.data.recv_status_on_client.trailing_metadata);
    c_op.data.recv_status_on_client.status =
        gpr_malloc(sizeof(grpc_status_code *));
    c_op.data.recv_status_on_client.status_details =
        gpr_malloc(sizeof(char *));
    *c_op.data.recv_status_on_client.status_details = NULL;
    c_op.data.recv_status_on_client.status_details_capacity =
        gpr_malloc(sizeof(size_t));
    *c_op.data.recv_status_on_client.status_details_capacity = 0;
    break;
  case GRPC_OP_RECV_CLOSE_ON_SERVER:
    c_op.data.recv_close_on_server.cancelled = gpr_malloc(sizeof(int));
    break;
  default:
    return 0;
  }
  *result = c_op;
  return 1;
}

void pygrpc_discard_op(grpc_op op) {
  size_t i;
  switch(op.op) {
  case GRPC_OP_SEND_INITIAL_METADATA:
    /* Whenever we produce send-metadata, we allocate new strings (to handle
       arbitrary sequence input as opposed to just lists or just tuples). We
       thus must free those elements. */
    for (i = 0; i < op.data.send_initial_metadata.count; ++i) {
      gpr_free((void *)op.data.send_initial_metadata.metadata[i].key);
      gpr_free((void *)op.data.send_initial_metadata.metadata[i].value);
    }
    gpr_free(op.data.send_initial_metadata.metadata);
    break;
  case GRPC_OP_SEND_MESSAGE:
    grpc_byte_buffer_destroy(op.data.send_message);
    break;
  case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
    /* Don't need to free any fields. */
    break;
  case GRPC_OP_SEND_STATUS_FROM_SERVER:
    /* Whenever we produce send-metadata, we allocate new strings (to handle
       arbitrary sequence input as opposed to just lists or just tuples). We
       thus must free those elements. */
    for (i = 0; i < op.data.send_status_from_server.trailing_metadata_count;
         ++i) {
      gpr_free(
          (void *)op.data.send_status_from_server.trailing_metadata[i].key);
      gpr_free(
          (void *)op.data.send_status_from_server.trailing_metadata[i].value);
    }
    gpr_free(op.data.send_status_from_server.trailing_metadata);
    gpr_free((char *)op.data.send_status_from_server.status_details);
    break;
  case GRPC_OP_RECV_INITIAL_METADATA:
    grpc_metadata_array_destroy(op.data.recv_initial_metadata);
    gpr_free(op.data.recv_initial_metadata);
    break;
  case GRPC_OP_RECV_MESSAGE:
    grpc_byte_buffer_destroy(*op.data.recv_message);
    gpr_free(op.data.recv_message);
    break;
  case GRPC_OP_RECV_STATUS_ON_CLIENT:
    grpc_metadata_array_destroy(op.data.recv_status_on_client.trailing_metadata);
    gpr_free(op.data.recv_status_on_client.trailing_metadata);
    gpr_free(op.data.recv_status_on_client.status);
    gpr_free(*op.data.recv_status_on_client.status_details);
    gpr_free(op.data.recv_status_on_client.status_details);
    gpr_free(op.data.recv_status_on_client.status_details_capacity);
    break;
  case GRPC_OP_RECV_CLOSE_ON_SERVER:
    gpr_free(op.data.recv_close_on_server.cancelled);
    break;
  }
}

PyObject *pygrpc_consume_ops(grpc_op *op, size_t nops) {
  static const int TYPE_INDEX = 0;
  static const int INITIAL_METADATA_INDEX = 1;
  static const int TRAILING_METADATA_INDEX = 2;
  static const int MESSAGE_INDEX = 3;
  static const int STATUS_INDEX = 4;
  static const int CANCELLED_INDEX = 5;
  static const int OPRESULT_LENGTH = 6;
  PyObject *list;
  size_t i;
  size_t j;
  char *bytes;
  size_t bytes_size;
  PyObject *results = PyList_New(nops);
  if (!results) {
    return NULL;
  }
  for (i = 0; i < nops; ++i) {
    PyObject *result = PyTuple_Pack(OPRESULT_LENGTH, Py_None, Py_None, Py_None,
                                    Py_None, Py_None, Py_None);
    PyTuple_SetItem(result, TYPE_INDEX, PyInt_FromLong(op[i].op));
    switch(op[i].op) {
    case GRPC_OP_RECV_INITIAL_METADATA:
      PyTuple_SetItem(result, INITIAL_METADATA_INDEX,
                      list=PyList_New(op[i].data.recv_initial_metadata->count));
      for (j = 0; j < op[i].data.recv_initial_metadata->count; ++j) {
        grpc_metadata md = op[i].data.recv_initial_metadata->metadata[j];
        PyList_SetItem(list, j, Py_BuildValue("ss#", md.key, md.value,
                                              (Py_ssize_t)md.value_length));
      }
      break;
    case GRPC_OP_RECV_MESSAGE:
      if (*op[i].data.recv_message) {
        pygrpc_byte_buffer_to_bytes(
            *op[i].data.recv_message, &bytes, &bytes_size);
        PyTuple_SetItem(result, MESSAGE_INDEX,
                        PyString_FromStringAndSize(bytes, bytes_size));
        gpr_free(bytes);
      } else {
        PyTuple_SetItem(result, MESSAGE_INDEX, Py_BuildValue(""));
      }
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      PyTuple_SetItem(
          result, TRAILING_METADATA_INDEX,
          list = PyList_New(op[i].data.recv_status_on_client.trailing_metadata->count));
      for (j = 0; j < op[i].data.recv_status_on_client.trailing_metadata->count; ++j) {
        grpc_metadata md =
            op[i].data.recv_status_on_client.trailing_metadata->metadata[j];
        PyList_SetItem(list, j, Py_BuildValue("ss#", md.key, md.value,
                                              (Py_ssize_t)md.value_length));
      }
      PyTuple_SetItem(
          result, STATUS_INDEX, Py_BuildValue(
              "is", *op[i].data.recv_status_on_client.status,
              *op[i].data.recv_status_on_client.status_details));
      break;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      PyTuple_SetItem(
          result, CANCELLED_INDEX,
          PyBool_FromLong(*op[i].data.recv_close_on_server.cancelled));
      break;
    default:
      break;
    }
    pygrpc_discard_op(op[i]);
    PyList_SetItem(results, i, result);
  }
  return results;
}

double pygrpc_cast_gpr_timespec_to_double(gpr_timespec timespec) {
  return timespec.tv_sec + 1e-9*timespec.tv_nsec;
}

/* Because C89 doesn't have a way to check for infinity... */
static int pygrpc_isinf(double x) {
  return x * 0 != 0;
}

gpr_timespec pygrpc_cast_double_to_gpr_timespec(double seconds) {
  gpr_timespec result;
  if (pygrpc_isinf(seconds)) {
    result = seconds > 0.0 ? gpr_inf_future(GPR_CLOCK_REALTIME)
                           : gpr_inf_past(GPR_CLOCK_REALTIME);
  } else {
    result.tv_sec = (time_t)seconds;
    result.tv_nsec = ((seconds - result.tv_sec) * 1e9);
    result.clock_type = GPR_CLOCK_REALTIME;
  }
  return result;
}

int pygrpc_produce_channel_args(PyObject *py_args, grpc_channel_args *c_args) {
  size_t num_args = PyList_Size(py_args);
  size_t i;
  grpc_channel_args args;
  args.num_args = num_args;
  args.args = gpr_malloc(sizeof(grpc_arg) * num_args);
  for (i = 0; i < args.num_args; ++i) {
    char *key;
    PyObject *value;
    if (!PyArg_ParseTuple(PyList_GetItem(py_args, i), "zO", &key, &value)) {
      gpr_free(args.args);
      args.num_args = 0;
      args.args = NULL;
      PyErr_SetString(PyExc_TypeError,
                      "expected a list of 2-tuple of str and str|int|None");
      return 0;
    }
    args.args[i].key = key;
    if (PyInt_Check(value)) {
      args.args[i].type = GRPC_ARG_INTEGER;
      args.args[i].value.integer = PyInt_AsLong(value);
    } else if (PyString_Check(value)) {
      args.args[i].type = GRPC_ARG_STRING;
      args.args[i].value.string = PyString_AsString(value);
    } else if (value == Py_None) {
      --args.num_args;
      --i;
      continue;
    } else {
      gpr_free(args.args);
      args.num_args = 0;
      args.args = NULL;
      PyErr_SetString(PyExc_TypeError,
                      "expected a list of 2-tuple of str and str|int|None");
      return 0;
    }
  }
  *c_args = args;
  return 1;
}

void pygrpc_discard_channel_args(grpc_channel_args args) {
  gpr_free(args.args);
}

int pygrpc_cast_pyseq_to_send_metadata(
    PyObject *pyseq, grpc_metadata **metadata, size_t *count) {
  size_t i;
  Py_ssize_t value_length;
  char *key;
  char *value;
  if (!PySequence_Check(pyseq)) {
    return 0;
  }
  *count = PySequence_Size(pyseq);
  *metadata = gpr_malloc(sizeof(grpc_metadata) * *count);
  for (i = 0; i < *count; ++i) {
    PyObject *item = PySequence_GetItem(pyseq, i);
    if (!PyArg_ParseTuple(item, "ss#", &key, &value, &value_length)) {
      Py_DECREF(item);
      gpr_free(*metadata);
      *count = 0;
      *metadata = NULL;
      return 0;
    } else {
      (*metadata)[i].key = gpr_strdup(key);
      (*metadata)[i].value = gpr_malloc(value_length);
      memcpy((void *)(*metadata)[i].value, value, value_length);
      Py_DECREF(item);
    }
    (*metadata)[i].value_length = value_length;
  }
  return 1;
}

PyObject *pygrpc_cast_metadata_array_to_pyseq(grpc_metadata_array metadata) {
  PyObject *result = PyTuple_New(metadata.count);
  size_t i;
  for (i = 0; i < metadata.count; ++i) {
    PyTuple_SetItem(
        result, i, Py_BuildValue(
            "ss#", metadata.metadata[i].key, metadata.metadata[i].value,
            (Py_ssize_t)metadata.metadata[i].value_length));
    if (PyErr_Occurred()) {
      Py_DECREF(result);
      return NULL;
    }
  }
  return result;
}

void pygrpc_byte_buffer_to_bytes(
    grpc_byte_buffer *buffer, char **result, size_t *result_size) {
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, buffer);
  gpr_slice slice;
  char *read_result = NULL;
  size_t size = 0;
  while (grpc_byte_buffer_reader_next(&reader, &slice)) {
    read_result = gpr_realloc(read_result, size + GPR_SLICE_LENGTH(slice));
    memcpy(read_result + size, GPR_SLICE_START_PTR(slice),
           GPR_SLICE_LENGTH(slice));
    size = size + GPR_SLICE_LENGTH(slice);
    gpr_slice_unref(slice);
  }
  *result_size = size;
  *result = read_result;
}
