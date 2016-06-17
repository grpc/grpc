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
#include "grpc/grpc_security.h"
#include "grpc/support/alloc.h"

#include "call.h"
#include "call_credentials.h"
#include "channel.h"
#include "channel_credentials.h"
#include "server.h"
#include "completion_queue_async_worker.h"
#include "server_credentials.h"

using v8::FunctionTemplate;
using v8::Local;
using v8::Value;
using v8::Object;
using v8::Uint32;
using v8::String;

static char *pem_root_certs = NULL;

void InitStatusConstants(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> status = Nan::New<Object>();
  Nan::Set(exports, Nan::New("status").ToLocalChecked(), status);
  Local<Value> OK(Nan::New<Uint32, uint32_t>(GRPC_STATUS_OK));
  Nan::Set(status, Nan::New("OK").ToLocalChecked(), OK);
  Local<Value> CANCELLED(Nan::New<Uint32, uint32_t>(GRPC_STATUS_CANCELLED));
  Nan::Set(status, Nan::New("CANCELLED").ToLocalChecked(), CANCELLED);
  Local<Value> UNKNOWN(Nan::New<Uint32, uint32_t>(GRPC_STATUS_UNKNOWN));
  Nan::Set(status, Nan::New("UNKNOWN").ToLocalChecked(), UNKNOWN);
  Local<Value> INVALID_ARGUMENT(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_INVALID_ARGUMENT));
  Nan::Set(status, Nan::New("INVALID_ARGUMENT").ToLocalChecked(),
           INVALID_ARGUMENT);
  Local<Value> DEADLINE_EXCEEDED(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_DEADLINE_EXCEEDED));
  Nan::Set(status, Nan::New("DEADLINE_EXCEEDED").ToLocalChecked(),
           DEADLINE_EXCEEDED);
  Local<Value> NOT_FOUND(Nan::New<Uint32, uint32_t>(GRPC_STATUS_NOT_FOUND));
  Nan::Set(status, Nan::New("NOT_FOUND").ToLocalChecked(), NOT_FOUND);
  Local<Value> ALREADY_EXISTS(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_ALREADY_EXISTS));
  Nan::Set(status, Nan::New("ALREADY_EXISTS").ToLocalChecked(), ALREADY_EXISTS);
  Local<Value> PERMISSION_DENIED(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_PERMISSION_DENIED));
  Nan::Set(status, Nan::New("PERMISSION_DENIED").ToLocalChecked(),
           PERMISSION_DENIED);
  Local<Value> UNAUTHENTICATED(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_UNAUTHENTICATED));
  Nan::Set(status, Nan::New("UNAUTHENTICATED").ToLocalChecked(),
           UNAUTHENTICATED);
  Local<Value> RESOURCE_EXHAUSTED(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_RESOURCE_EXHAUSTED));
  Nan::Set(status, Nan::New("RESOURCE_EXHAUSTED").ToLocalChecked(),
           RESOURCE_EXHAUSTED);
  Local<Value> FAILED_PRECONDITION(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_FAILED_PRECONDITION));
  Nan::Set(status, Nan::New("FAILED_PRECONDITION").ToLocalChecked(),
           FAILED_PRECONDITION);
  Local<Value> ABORTED(Nan::New<Uint32, uint32_t>(GRPC_STATUS_ABORTED));
  Nan::Set(status, Nan::New("ABORTED").ToLocalChecked(), ABORTED);
  Local<Value> OUT_OF_RANGE(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_OUT_OF_RANGE));
  Nan::Set(status, Nan::New("OUT_OF_RANGE").ToLocalChecked(), OUT_OF_RANGE);
  Local<Value> UNIMPLEMENTED(
      Nan::New<Uint32, uint32_t>(GRPC_STATUS_UNIMPLEMENTED));
  Nan::Set(status, Nan::New("UNIMPLEMENTED").ToLocalChecked(), UNIMPLEMENTED);
  Local<Value> INTERNAL(Nan::New<Uint32, uint32_t>(GRPC_STATUS_INTERNAL));
  Nan::Set(status, Nan::New("INTERNAL").ToLocalChecked(), INTERNAL);
  Local<Value> UNAVAILABLE(Nan::New<Uint32, uint32_t>(GRPC_STATUS_UNAVAILABLE));
  Nan::Set(status, Nan::New("UNAVAILABLE").ToLocalChecked(), UNAVAILABLE);
  Local<Value> DATA_LOSS(Nan::New<Uint32, uint32_t>(GRPC_STATUS_DATA_LOSS));
  Nan::Set(status, Nan::New("DATA_LOSS").ToLocalChecked(), DATA_LOSS);
}

void InitCallErrorConstants(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> call_error = Nan::New<Object>();
  Nan::Set(exports, Nan::New("callError").ToLocalChecked(), call_error);
  Local<Value> OK(Nan::New<Uint32, uint32_t>(GRPC_CALL_OK));
  Nan::Set(call_error, Nan::New("OK").ToLocalChecked(), OK);
  Local<Value> CALL_ERROR(Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR));
  Nan::Set(call_error, Nan::New("ERROR").ToLocalChecked(), CALL_ERROR);
  Local<Value> NOT_ON_SERVER(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_NOT_ON_SERVER));
  Nan::Set(call_error, Nan::New("NOT_ON_SERVER").ToLocalChecked(),
           NOT_ON_SERVER);
  Local<Value> NOT_ON_CLIENT(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_NOT_ON_CLIENT));
  Nan::Set(call_error, Nan::New("NOT_ON_CLIENT").ToLocalChecked(),
           NOT_ON_CLIENT);
  Local<Value> ALREADY_INVOKED(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_ALREADY_INVOKED));
  Nan::Set(call_error, Nan::New("ALREADY_INVOKED").ToLocalChecked(),
           ALREADY_INVOKED);
  Local<Value> NOT_INVOKED(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_NOT_INVOKED));
  Nan::Set(call_error, Nan::New("NOT_INVOKED").ToLocalChecked(), NOT_INVOKED);
  Local<Value> ALREADY_FINISHED(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_ALREADY_FINISHED));
  Nan::Set(call_error, Nan::New("ALREADY_FINISHED").ToLocalChecked(),
           ALREADY_FINISHED);
  Local<Value> TOO_MANY_OPERATIONS(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS));
  Nan::Set(call_error, Nan::New("TOO_MANY_OPERATIONS").ToLocalChecked(),
           TOO_MANY_OPERATIONS);
  Local<Value> INVALID_FLAGS(
      Nan::New<Uint32, uint32_t>(GRPC_CALL_ERROR_INVALID_FLAGS));
  Nan::Set(call_error, Nan::New("INVALID_FLAGS").ToLocalChecked(),
           INVALID_FLAGS);
}

void InitOpTypeConstants(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> op_type = Nan::New<Object>();
  Nan::Set(exports, Nan::New("opType").ToLocalChecked(), op_type);
  Local<Value> SEND_INITIAL_METADATA(
      Nan::New<Uint32, uint32_t>(GRPC_OP_SEND_INITIAL_METADATA));
  Nan::Set(op_type, Nan::New("SEND_INITIAL_METADATA").ToLocalChecked(),
           SEND_INITIAL_METADATA);
  Local<Value> SEND_MESSAGE(
      Nan::New<Uint32, uint32_t>(GRPC_OP_SEND_MESSAGE));
  Nan::Set(op_type, Nan::New("SEND_MESSAGE").ToLocalChecked(), SEND_MESSAGE);
  Local<Value> SEND_CLOSE_FROM_CLIENT(
      Nan::New<Uint32, uint32_t>(GRPC_OP_SEND_CLOSE_FROM_CLIENT));
  Nan::Set(op_type, Nan::New("SEND_CLOSE_FROM_CLIENT").ToLocalChecked(),
           SEND_CLOSE_FROM_CLIENT);
  Local<Value> SEND_STATUS_FROM_SERVER(
      Nan::New<Uint32, uint32_t>(GRPC_OP_SEND_STATUS_FROM_SERVER));
  Nan::Set(op_type, Nan::New("SEND_STATUS_FROM_SERVER").ToLocalChecked(),
           SEND_STATUS_FROM_SERVER);
  Local<Value> RECV_INITIAL_METADATA(
      Nan::New<Uint32, uint32_t>(GRPC_OP_RECV_INITIAL_METADATA));
  Nan::Set(op_type, Nan::New("RECV_INITIAL_METADATA").ToLocalChecked(),
           RECV_INITIAL_METADATA);
  Local<Value> RECV_MESSAGE(
      Nan::New<Uint32, uint32_t>(GRPC_OP_RECV_MESSAGE));
  Nan::Set(op_type, Nan::New("RECV_MESSAGE").ToLocalChecked(), RECV_MESSAGE);
  Local<Value> RECV_STATUS_ON_CLIENT(
      Nan::New<Uint32, uint32_t>(GRPC_OP_RECV_STATUS_ON_CLIENT));
  Nan::Set(op_type, Nan::New("RECV_STATUS_ON_CLIENT").ToLocalChecked(),
           RECV_STATUS_ON_CLIENT);
  Local<Value> RECV_CLOSE_ON_SERVER(
      Nan::New<Uint32, uint32_t>(GRPC_OP_RECV_CLOSE_ON_SERVER));
  Nan::Set(op_type, Nan::New("RECV_CLOSE_ON_SERVER").ToLocalChecked(),
           RECV_CLOSE_ON_SERVER);
}

void InitPropagateConstants(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> propagate = Nan::New<Object>();
  Nan::Set(exports, Nan::New("propagate").ToLocalChecked(), propagate);
  Local<Value> DEADLINE(Nan::New<Uint32, uint32_t>(GRPC_PROPAGATE_DEADLINE));
  Nan::Set(propagate, Nan::New("DEADLINE").ToLocalChecked(), DEADLINE);
  Local<Value> CENSUS_STATS_CONTEXT(
      Nan::New<Uint32, uint32_t>(GRPC_PROPAGATE_CENSUS_STATS_CONTEXT));
  Nan::Set(propagate, Nan::New("CENSUS_STATS_CONTEXT").ToLocalChecked(),
           CENSUS_STATS_CONTEXT);
  Local<Value> CENSUS_TRACING_CONTEXT(
      Nan::New<Uint32, uint32_t>(GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT));
  Nan::Set(propagate, Nan::New("CENSUS_TRACING_CONTEXT").ToLocalChecked(),
           CENSUS_TRACING_CONTEXT);
  Local<Value> CANCELLATION(
      Nan::New<Uint32, uint32_t>(GRPC_PROPAGATE_CANCELLATION));
  Nan::Set(propagate, Nan::New("CANCELLATION").ToLocalChecked(), CANCELLATION);
  Local<Value> DEFAULTS(Nan::New<Uint32, uint32_t>(GRPC_PROPAGATE_DEFAULTS));
  Nan::Set(propagate, Nan::New("DEFAULTS").ToLocalChecked(), DEFAULTS);
}

void InitConnectivityStateConstants(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> channel_state = Nan::New<Object>();
  Nan::Set(exports, Nan::New("connectivityState").ToLocalChecked(),
           channel_state);
  Local<Value> IDLE(Nan::New<Uint32, uint32_t>(GRPC_CHANNEL_IDLE));
  Nan::Set(channel_state, Nan::New("IDLE").ToLocalChecked(), IDLE);
  Local<Value> CONNECTING(Nan::New<Uint32, uint32_t>(GRPC_CHANNEL_CONNECTING));
  Nan::Set(channel_state, Nan::New("CONNECTING").ToLocalChecked(), CONNECTING);
  Local<Value> READY(Nan::New<Uint32, uint32_t>(GRPC_CHANNEL_READY));
  Nan::Set(channel_state, Nan::New("READY").ToLocalChecked(), READY);
  Local<Value> TRANSIENT_FAILURE(
      Nan::New<Uint32, uint32_t>(GRPC_CHANNEL_TRANSIENT_FAILURE));
  Nan::Set(channel_state, Nan::New("TRANSIENT_FAILURE").ToLocalChecked(),
           TRANSIENT_FAILURE);
  Local<Value> FATAL_FAILURE(
      Nan::New<Uint32, uint32_t>(GRPC_CHANNEL_SHUTDOWN));
  Nan::Set(channel_state, Nan::New("FATAL_FAILURE").ToLocalChecked(),
           FATAL_FAILURE);
}

void InitWriteFlags(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> write_flags = Nan::New<Object>();
  Nan::Set(exports, Nan::New("writeFlags").ToLocalChecked(), write_flags);
  Local<Value> BUFFER_HINT(Nan::New<Uint32, uint32_t>(GRPC_WRITE_BUFFER_HINT));
  Nan::Set(write_flags, Nan::New("BUFFER_HINT").ToLocalChecked(), BUFFER_HINT);
  Local<Value> NO_COMPRESS(Nan::New<Uint32, uint32_t>(GRPC_WRITE_NO_COMPRESS));
  Nan::Set(write_flags, Nan::New("NO_COMPRESS").ToLocalChecked(), NO_COMPRESS);
}

NAN_METHOD(MetadataKeyIsLegal) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "headerKeyIsLegal's argument must be a string");
  }
  Local<String> key = Nan::To<String>(info[0]).ToLocalChecked();
  Nan::Utf8String key_utf8_str(key);
  char *key_str = *key_utf8_str;
  info.GetReturnValue().Set(static_cast<bool>(
      grpc_header_key_is_legal(key_str, static_cast<size_t>(key->Length()))));
}

NAN_METHOD(MetadataNonbinValueIsLegal) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "metadataNonbinValueIsLegal's argument must be a string");
  }
  Local<String> value = Nan::To<String>(info[0]).ToLocalChecked();
  Nan::Utf8String value_utf8_str(value);
  char *value_str = *value_utf8_str;
  info.GetReturnValue().Set(static_cast<bool>(
      grpc_header_nonbin_value_is_legal(
          value_str, static_cast<size_t>(value->Length()))));
}

NAN_METHOD(MetadataKeyIsBinary) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "metadataKeyIsLegal's argument must be a string");
  }
  Local<String> key = Nan::To<String>(info[0]).ToLocalChecked();
  Nan::Utf8String key_utf8_str(key);
  char *key_str = *key_utf8_str;
  info.GetReturnValue().Set(static_cast<bool>(
      grpc_is_binary_header(key_str, static_cast<size_t>(key->Length()))));
}

static grpc_ssl_roots_override_result get_ssl_roots_override(
    char **pem_root_certs_ptr) {
  *pem_root_certs_ptr = pem_root_certs;
  if (pem_root_certs == NULL) {
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  } else {
    return GRPC_SSL_ROOTS_OVERRIDE_OK;
  }
}

/* This should only be called once, and only before creating any
 *ServerCredentials */
NAN_METHOD(SetDefaultRootsPem) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "setDefaultRootsPem's argument must be a string");
  }
  Nan::Utf8String utf8_roots(info[0]);
  size_t length = static_cast<size_t>(utf8_roots.length());
  if (length > 0) {
    const char *data = *utf8_roots;
    pem_root_certs = (char *)gpr_malloc((length + 1) * sizeof(char));
    memcpy(pem_root_certs, data, length + 1);
  }
}

void init(Local<Object> exports) {
  Nan::HandleScope scope;
  grpc_init();
  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);
  InitStatusConstants(exports);
  InitCallErrorConstants(exports);
  InitOpTypeConstants(exports);
  InitPropagateConstants(exports);
  InitConnectivityStateConstants(exports);
  InitWriteFlags(exports);

  grpc::node::Call::Init(exports);
  grpc::node::CallCredentials::Init(exports);
  grpc::node::Channel::Init(exports);
  grpc::node::ChannelCredentials::Init(exports);
  grpc::node::Server::Init(exports);
  grpc::node::CompletionQueueAsyncWorker::Init(exports);
  grpc::node::ServerCredentials::Init(exports);

  // Attach a few utility functions directly to the module
  Nan::Set(exports, Nan::New("metadataKeyIsLegal").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(MetadataKeyIsLegal)).ToLocalChecked());
  Nan::Set(exports, Nan::New("metadataNonbinValueIsLegal").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(MetadataNonbinValueIsLegal)
                            ).ToLocalChecked());
  Nan::Set(exports, Nan::New("metadataKeyIsBinary").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(MetadataKeyIsBinary)
                            ).ToLocalChecked());
  Nan::Set(exports, Nan::New("setDefaultRootsPem").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(SetDefaultRootsPem)
                            ).ToLocalChecked());
}

NODE_MODULE(grpc_node, init)
