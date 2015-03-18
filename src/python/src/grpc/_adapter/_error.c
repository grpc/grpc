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

#include "grpc/_adapter/_error.h"

#include <Python.h>
#include <grpc/grpc.h>

const PyObject *pygrpc_translate_call_error(grpc_call_error call_error) {
  switch (call_error) {
    case GRPC_CALL_OK:
      Py_RETURN_NONE;
    case GRPC_CALL_ERROR:
      PyErr_SetString(PyExc_Exception, "Defect: unknown defect!");
      return NULL;
    case GRPC_CALL_ERROR_NOT_ON_SERVER:
      PyErr_SetString(PyExc_Exception,
                      "Defect: client-only method called on server!");
      return NULL;
    case GRPC_CALL_ERROR_NOT_ON_CLIENT:
      PyErr_SetString(PyExc_Exception,
                      "Defect: server-only method called on client!");
      return NULL;
    case GRPC_CALL_ERROR_ALREADY_ACCEPTED:
      PyErr_SetString(PyExc_Exception,
                      "Defect: attempted to accept already-accepted call!");
      return NULL;
    case GRPC_CALL_ERROR_ALREADY_INVOKED:
      PyErr_SetString(PyExc_Exception,
                      "Defect: attempted to invoke already-invoked call!");
      return NULL;
    case GRPC_CALL_ERROR_NOT_INVOKED:
      PyErr_SetString(PyExc_Exception, "Defect: Call not yet invoked!");
      return NULL;
    case GRPC_CALL_ERROR_ALREADY_FINISHED:
      PyErr_SetString(PyExc_Exception, "Defect: Call already finished!");
      return NULL;
    case GRPC_CALL_ERROR_TOO_MANY_OPERATIONS:
      PyErr_SetString(PyExc_Exception,
                      "Defect: Attempted extra read or extra write on call!");
      return NULL;
    case GRPC_CALL_ERROR_INVALID_FLAGS:
      PyErr_SetString(PyExc_Exception, "Defect: invalid flags!");
      return NULL;
    default:
      PyErr_SetString(PyExc_Exception, "Defect: Unknown call error!");
      return NULL;
  }
}
