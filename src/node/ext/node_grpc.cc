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

#include <node.h>
#include <nan.h>
#include <v8.h>
#include "grpc/grpc.h"

#include "call.h"
#include "channel.h"
#include "server.h"
#include "completion_queue_async_worker.h"
#include "credentials.h"
#include "server_credentials.h"

using v8::Handle;
using v8::Value;
using v8::Object;
using v8::Uint32;
using v8::String;

void InitStatusConstants(Handle<Object> exports) {
  NanScope();
  Handle<Object> status = NanNew<Object>();
  exports->Set(NanNew("status"), status);
  Handle<Value> OK(NanNew<Uint32, uint32_t>(GRPC_STATUS_OK));
  status->Set(NanNew("OK"), OK);
  Handle<Value> CANCELLED(NanNew<Uint32, uint32_t>(GRPC_STATUS_CANCELLED));
  status->Set(NanNew("CANCELLED"), CANCELLED);
  Handle<Value> UNKNOWN(NanNew<Uint32, uint32_t>(GRPC_STATUS_UNKNOWN));
  status->Set(NanNew("UNKNOWN"), UNKNOWN);
  Handle<Value> INVALID_ARGUMENT(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_INVALID_ARGUMENT));
  status->Set(NanNew("INVALID_ARGUMENT"), INVALID_ARGUMENT);
  Handle<Value> DEADLINE_EXCEEDED(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_DEADLINE_EXCEEDED));
  status->Set(NanNew("DEADLINE_EXCEEDED"), DEADLINE_EXCEEDED);
  Handle<Value> NOT_FOUND(NanNew<Uint32, uint32_t>(GRPC_STATUS_NOT_FOUND));
  status->Set(NanNew("NOT_FOUND"), NOT_FOUND);
  Handle<Value> ALREADY_EXISTS(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_ALREADY_EXISTS));
  status->Set(NanNew("ALREADY_EXISTS"), ALREADY_EXISTS);
  Handle<Value> PERMISSION_DENIED(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_PERMISSION_DENIED));
  status->Set(NanNew("PERMISSION_DENIED"), PERMISSION_DENIED);
  Handle<Value> UNAUTHENTICATED(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_UNAUTHENTICATED));
  status->Set(NanNew("UNAUTHENTICATED"), UNAUTHENTICATED);
  Handle<Value> RESOURCE_EXHAUSTED(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_RESOURCE_EXHAUSTED));
  status->Set(NanNew("RESOURCE_EXHAUSTED"), RESOURCE_EXHAUSTED);
  Handle<Value> FAILED_PRECONDITION(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_FAILED_PRECONDITION));
  status->Set(NanNew("FAILED_PRECONDITION"), FAILED_PRECONDITION);
  Handle<Value> ABORTED(NanNew<Uint32, uint32_t>(GRPC_STATUS_ABORTED));
  status->Set(NanNew("ABORTED"), ABORTED);
  Handle<Value> OUT_OF_RANGE(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_OUT_OF_RANGE));
  status->Set(NanNew("OUT_OF_RANGE"), OUT_OF_RANGE);
  Handle<Value> UNIMPLEMENTED(
      NanNew<Uint32, uint32_t>(GRPC_STATUS_UNIMPLEMENTED));
  status->Set(NanNew("UNIMPLEMENTED"), UNIMPLEMENTED);
  Handle<Value> INTERNAL(NanNew<Uint32, uint32_t>(GRPC_STATUS_INTERNAL));
  status->Set(NanNew("INTERNAL"), INTERNAL);
  Handle<Value> UNAVAILABLE(NanNew<Uint32, uint32_t>(GRPC_STATUS_UNAVAILABLE));
  status->Set(NanNew("UNAVAILABLE"), UNAVAILABLE);
  Handle<Value> DATA_LOSS(NanNew<Uint32, uint32_t>(GRPC_STATUS_DATA_LOSS));
  status->Set(NanNew("DATA_LOSS"), DATA_LOSS);
}

void InitCallErrorConstants(Handle<Object> exports) {
  NanScope();
  Handle<Object> call_error = NanNew<Object>();
  exports->Set(NanNew("callError"), call_error);
  Handle<Value> OK(NanNew<Uint32, uint32_t>(GRPC_CALL_OK));
  call_error->Set(NanNew("OK"), OK);
  Handle<Value> ERROR(NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR));
  call_error->Set(NanNew("ERROR"), ERROR);
  Handle<Value> NOT_ON_SERVER(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_NOT_ON_SERVER));
  call_error->Set(NanNew("NOT_ON_SERVER"), NOT_ON_SERVER);
  Handle<Value> NOT_ON_CLIENT(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_NOT_ON_CLIENT));
  call_error->Set(NanNew("NOT_ON_CLIENT"), NOT_ON_CLIENT);
  Handle<Value> ALREADY_INVOKED(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_ALREADY_INVOKED));
  call_error->Set(NanNew("ALREADY_INVOKED"), ALREADY_INVOKED);
  Handle<Value> NOT_INVOKED(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_NOT_INVOKED));
  call_error->Set(NanNew("NOT_INVOKED"), NOT_INVOKED);
  Handle<Value> ALREADY_FINISHED(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_ALREADY_FINISHED));
  call_error->Set(NanNew("ALREADY_FINISHED"), ALREADY_FINISHED);
  Handle<Value> TOO_MANY_OPERATIONS(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS));
  call_error->Set(NanNew("TOO_MANY_OPERATIONS"), TOO_MANY_OPERATIONS);
  Handle<Value> INVALID_FLAGS(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_INVALID_FLAGS));
  call_error->Set(NanNew("INVALID_FLAGS"), INVALID_FLAGS);
}

void InitOpTypeConstants(Handle<Object> exports) {
  NanScope();
  Handle<Object> op_type = NanNew<Object>();
  exports->Set(NanNew("opType"), op_type);
  Handle<Value> SEND_INITIAL_METADATA(
      NanNew<Uint32, uint32_t>(GRPC_OP_SEND_INITIAL_METADATA));
  op_type->Set(NanNew("SEND_INITIAL_METADATA"), SEND_INITIAL_METADATA);
  Handle<Value> SEND_MESSAGE(
      NanNew<Uint32, uint32_t>(GRPC_OP_SEND_MESSAGE));
  op_type->Set(NanNew("SEND_MESSAGE"), SEND_MESSAGE);
  Handle<Value> SEND_CLOSE_FROM_CLIENT(
      NanNew<Uint32, uint32_t>(GRPC_OP_SEND_CLOSE_FROM_CLIENT));
  op_type->Set(NanNew("SEND_CLOSE_FROM_CLIENT"), SEND_CLOSE_FROM_CLIENT);
  Handle<Value> SEND_STATUS_FROM_SERVER(
      NanNew<Uint32, uint32_t>(GRPC_OP_SEND_STATUS_FROM_SERVER));
  op_type->Set(NanNew("SEND_STATUS_FROM_SERVER"), SEND_STATUS_FROM_SERVER);
  Handle<Value> RECV_INITIAL_METADATA(
      NanNew<Uint32, uint32_t>(GRPC_OP_RECV_INITIAL_METADATA));
  op_type->Set(NanNew("RECV_INITIAL_METADATA"), RECV_INITIAL_METADATA);
  Handle<Value> RECV_MESSAGE(
      NanNew<Uint32, uint32_t>(GRPC_OP_RECV_MESSAGE));
  op_type->Set(NanNew("RECV_MESSAGE"), RECV_MESSAGE);
  Handle<Value> RECV_STATUS_ON_CLIENT(
      NanNew<Uint32, uint32_t>(GRPC_OP_RECV_STATUS_ON_CLIENT));
  op_type->Set(NanNew("RECV_STATUS_ON_CLIENT"), RECV_STATUS_ON_CLIENT);
  Handle<Value> RECV_CLOSE_ON_SERVER(
      NanNew<Uint32, uint32_t>(GRPC_OP_RECV_CLOSE_ON_SERVER));
  op_type->Set(NanNew("RECV_CLOSE_ON_SERVER"), RECV_CLOSE_ON_SERVER);
}

void InitConnectivityStateConstants(Handle<Object> exports) {
  NanScope();
  Handle<Object> channel_state = NanNew<Object>();
  exports->Set(NanNew("connectivityState"), channel_state);
  Handle<Value> IDLE(NanNew<Uint32, uint32_t>(GRPC_CHANNEL_IDLE));
  channel_state->Set(NanNew("IDLE"), IDLE);
  Handle<Value> CONNECTING(NanNew<Uint32, uint32_t>(GRPC_CHANNEL_CONNECTING));
  channel_state->Set(NanNew("CONNECTING"), CONNECTING);
  Handle<Value> READY(NanNew<Uint32, uint32_t>(GRPC_CHANNEL_READY));
  channel_state->Set(NanNew("READY"), READY);
  Handle<Value> TRANSIENT_FAILURE(
      NanNew<Uint32, uint32_t>(GRPC_CHANNEL_TRANSIENT_FAILURE));
  channel_state->Set(NanNew("TRANSIENT_FAILURE"), TRANSIENT_FAILURE);
  Handle<Value> FATAL_FAILURE(
      NanNew<Uint32, uint32_t>(GRPC_CHANNEL_FATAL_FAILURE));
  channel_state->Set(NanNew("FATAL_FAILURE"), FATAL_FAILURE);
}

void init(Handle<Object> exports) {
  NanScope();
  grpc_init();
  InitStatusConstants(exports);
  InitCallErrorConstants(exports);
  InitOpTypeConstants(exports);
  InitConnectivityStateConstants(exports);

  grpc::node::Call::Init(exports);
  grpc::node::Channel::Init(exports);
  grpc::node::Server::Init(exports);
  grpc::node::CompletionQueueAsyncWorker::Init(exports);
  grpc::node::Credentials::Init(exports);
  grpc::node::ServerCredentials::Init(exports);
}

NODE_MODULE(grpc, init)
