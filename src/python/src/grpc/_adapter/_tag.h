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

#ifndef _ADAPTER__TAG_H_
#define _ADAPTER__TAG_H_

#include <Python.h>
#include <grpc/grpc.h>

#include "grpc/_adapter/_call.h"
#include "grpc/_adapter/_completion_queue.h"

/* grpc_completion_type is becoming meaningless in grpc_event; this is a partial
   replacement for its descriptive functionality until Python can move its whole
   C and C adapter stack to more closely resemble the core batching API. */
typedef enum {
  PYGRPC_SERVER_RPC_NEW       = 0,
  PYGRPC_INITIAL_METADATA     = 1,
  PYGRPC_READ                 = 2,
  PYGRPC_WRITE_ACCEPTED       = 3,
  PYGRPC_FINISH_ACCEPTED      = 4,
  PYGRPC_CLIENT_METADATA_READ = 5,
  PYGRPC_FINISHED_CLIENT      = 6,
  PYGRPC_FINISHED_SERVER      = 7
} pygrpc_tag_type;

typedef struct {
  pygrpc_tag_type type;
  PyObject *user_tag;

  Call *call;
} pygrpc_tag;

pygrpc_tag *pygrpc_tag_new(pygrpc_tag_type type, PyObject *user_tag,
                           Call *call);
pygrpc_tag *pygrpc_tag_new_server_rpc_call(PyObject *user_tag);
void pygrpc_tag_destroy(pygrpc_tag *self);

#endif /* _ADAPTER__TAG_H_ */

