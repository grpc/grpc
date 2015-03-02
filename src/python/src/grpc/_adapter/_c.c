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

#include <Python.h>
#include <grpc/grpc.h>

#include "grpc/_adapter/_completion_queue.h"
#include "grpc/_adapter/_channel.h"
#include "grpc/_adapter/_call.h"
#include "grpc/_adapter/_server.h"
#include "grpc/_adapter/_client_credentials.h"
#include "grpc/_adapter/_server_credentials.h"

static PyObject *init(PyObject *self) {
  grpc_init();
  Py_RETURN_NONE;
}

static PyObject *shutdown(PyObject *self) {
  grpc_shutdown();
  Py_RETURN_NONE;
}

static PyMethodDef _c_methods[] = {
    {"init", (PyCFunction)init, METH_NOARGS,
     "Initialize the module's static state."},
    {"shut_down", (PyCFunction)shutdown, METH_NOARGS,
     "Shut down the module's static state."},
    {NULL},
};

PyMODINIT_FUNC init_c(void) {
  PyObject *module;

  module = Py_InitModule3("_c", _c_methods,
                          "Wrappings of C structures and functions.");

  if (pygrpc_add_completion_queue(module) == -1) {
    return;
  }
  if (pygrpc_add_channel(module) == -1) {
    return;
  }
  if (pygrpc_add_call(module) == -1) {
    return;
  }
  if (pygrpc_add_server(module) == -1) {
    return;
  }
  if (pygrpc_add_client_credentials(module) == -1) {
    return;
  }
  if (pygrpc_add_server_credentials(module) == -1) {
    return;
  }
}
