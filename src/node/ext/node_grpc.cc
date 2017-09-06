/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <queue>

#include <nan.h>
#include <node.h>
#include <v8.h>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/alloc.h"
#include "grpc/support/log.h"
#include "grpc/support/time.h"

// TODO(murgatroid99): Remove this when the endpoint API becomes public
extern "C" {
#include "src/core/lib/iomgr/pollset_uv.h"
}

#include "call.h"
#include "call_credentials.h"
#include "channel.h"
#include "channel_credentials.h"
#include "completion_queue.h"
#include "server.h"
#include "server_credentials.h"
#include "slice.h"
#include "timeval.h"

using grpc::node::CreateSliceFromString;

using v8::FunctionTemplate;
using v8::Local;
using v8::Value;
using v8::Number;
using v8::Object;
using v8::Uint32;
using v8::String;

typedef struct log_args {
  gpr_log_func_args core_args;
  gpr_timespec timestamp;
} log_args;

typedef struct logger_state {
  Nan::Callback *callback;
  std::queue<log_args *> *pending_args;
  uv_mutex_t mutex;
  uv_async_t async;
  // Indicates that a logger has been set
  bool logger_set;
} logger_state;

logger_state grpc_logger_state;

static char *pem_root_certs = NULL;

void InitOpTypeConstants(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<Object> op_type = Nan::New<Object>();
  Nan::Set(exports, Nan::New("opType").ToLocalChecked(), op_type);
  Local<Value> SEND_INITIAL_METADATA(
      Nan::New<Uint32, uint32_t>(GRPC_OP_SEND_INITIAL_METADATA));
  Nan::Set(op_type, Nan::New("SEND_INITIAL_METADATA").ToLocalChecked(),
           SEND_INITIAL_METADATA);
  Local<Value> SEND_MESSAGE(Nan::New<Uint32, uint32_t>(GRPC_OP_SEND_MESSAGE));
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
  Local<Value> RECV_MESSAGE(Nan::New<Uint32, uint32_t>(GRPC_OP_RECV_MESSAGE));
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
  Local<Value> FATAL_FAILURE(Nan::New<Uint32, uint32_t>(GRPC_CHANNEL_SHUTDOWN));
  Nan::Set(channel_state, Nan::New("FATAL_FAILURE").ToLocalChecked(),
           FATAL_FAILURE);
}

NAN_METHOD(MetadataKeyIsLegal) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError("headerKeyIsLegal's argument must be a string");
  }
  Local<String> key = Nan::To<String>(info[0]).ToLocalChecked();
  grpc_slice slice = CreateSliceFromString(key);
  info.GetReturnValue().Set(static_cast<bool>(grpc_header_key_is_legal(slice)));
  grpc_slice_unref(slice);
}

NAN_METHOD(MetadataNonbinValueIsLegal) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "metadataNonbinValueIsLegal's argument must be a string");
  }
  Local<String> value = Nan::To<String>(info[0]).ToLocalChecked();
  grpc_slice slice = CreateSliceFromString(value);
  info.GetReturnValue().Set(
      static_cast<bool>(grpc_header_nonbin_value_is_legal(slice)));
  grpc_slice_unref(slice);
}

NAN_METHOD(MetadataKeyIsBinary) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "metadataKeyIsLegal's argument must be a string");
  }
  Local<String> key = Nan::To<String>(info[0]).ToLocalChecked();
  grpc_slice slice = CreateSliceFromString(key);
  info.GetReturnValue().Set(static_cast<bool>(grpc_is_binary_header(slice)));
  grpc_slice_unref(slice);
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

NAUV_WORK_CB(LogMessagesCallback) {
  Nan::HandleScope scope;
  std::queue<log_args *> args;
  uv_mutex_lock(&grpc_logger_state.mutex);
  grpc_logger_state.pending_args->swap(args);
  uv_mutex_unlock(&grpc_logger_state.mutex);
  /* Call the callback with each log message */
  while (!args.empty()) {
    log_args *arg = args.front();
    args.pop();
    Local<Value> file = Nan::New(arg->core_args.file).ToLocalChecked();
    Local<Value> line = Nan::New<Uint32, uint32_t>(arg->core_args.line);
    Local<Value> severity =
        Nan::New(gpr_log_severity_string(arg->core_args.severity))
            .ToLocalChecked();
    Local<Value> message = Nan::New(arg->core_args.message).ToLocalChecked();
    Local<Value> timestamp =
        Nan::New<v8::Date>(grpc::node::TimespecToMilliseconds(arg->timestamp))
            .ToLocalChecked();
    const int argc = 5;
    Local<Value> argv[argc] = {file, line, severity, message, timestamp};
    grpc_logger_state.callback->Call(argc, argv);
    delete[] arg->core_args.message;
    delete arg;
  }
}

void node_log_func(gpr_log_func_args *args) {
  // TODO(mlumish): Use the core's log formatter when it becomes available
  log_args *args_copy = new log_args;
  size_t message_len = strlen(args->message) + 1;
  char *message = new char[message_len];
  memcpy(message, args->message, message_len);
  memcpy(&args_copy->core_args, args, sizeof(gpr_log_func_args));
  args_copy->core_args.message = message;
  args_copy->timestamp = gpr_now(GPR_CLOCK_REALTIME);

  uv_mutex_lock(&grpc_logger_state.mutex);
  grpc_logger_state.pending_args->push(args_copy);
  uv_mutex_unlock(&grpc_logger_state.mutex);

  uv_async_send(&grpc_logger_state.async);
}

void init_logger() {
  memset(&grpc_logger_state, 0, sizeof(logger_state));
  grpc_logger_state.pending_args = new std::queue<log_args *>();
  uv_mutex_init(&grpc_logger_state.mutex);
  uv_async_init(uv_default_loop(), &grpc_logger_state.async,
                LogMessagesCallback);
  uv_unref((uv_handle_t *)&grpc_logger_state.async);
  grpc_logger_state.logger_set = false;

  gpr_log_verbosity_init();
}

/* This registers a JavaScript logger for messages from the gRPC core. Because
   that handler has to be run in the context of the JavaScript event loop, it
   will be run asynchronously. To minimize the problems that could cause for
   debugging, we leave core to do its default synchronous logging until a
   JavaScript logger is set */
NAN_METHOD(SetDefaultLoggerCallback) {
  if (!info[0]->IsFunction()) {
    return Nan::ThrowTypeError(
        "setDefaultLoggerCallback's argument must be a function");
  }
  if (!grpc_logger_state.logger_set) {
    gpr_set_log_function(node_log_func);
    grpc_logger_state.logger_set = true;
  }
  grpc_logger_state.callback = new Nan::Callback(info[0].As<v8::Function>());
}

NAN_METHOD(SetLogVerbosity) {
  if (!info[0]->IsUint32()) {
    return Nan::ThrowTypeError("setLogVerbosity's argument must be a number");
  }
  gpr_log_severity severity =
      static_cast<gpr_log_severity>(Nan::To<uint32_t>(info[0]).FromJust());
  gpr_set_log_verbosity(severity);
}

void init(Local<Object> exports) {
  Nan::HandleScope scope;
  grpc_init();
  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);
  init_logger();

  InitOpTypeConstants(exports);
  InitConnectivityStateConstants(exports);

  grpc_pollset_work_run_loop = 0;

  grpc::node::Call::Init(exports);
  grpc::node::CallCredentials::Init(exports);
  grpc::node::Channel::Init(exports);
  grpc::node::ChannelCredentials::Init(exports);
  grpc::node::Server::Init(exports);
  grpc::node::ServerCredentials::Init(exports);

  grpc::node::CompletionQueueInit(exports);

  // Attach a few utility functions directly to the module
  Nan::Set(exports, Nan::New("metadataKeyIsLegal").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(MetadataKeyIsLegal))
               .ToLocalChecked());
  Nan::Set(
      exports, Nan::New("metadataNonbinValueIsLegal").ToLocalChecked(),
      Nan::GetFunction(Nan::New<FunctionTemplate>(MetadataNonbinValueIsLegal))
          .ToLocalChecked());
  Nan::Set(exports, Nan::New("metadataKeyIsBinary").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(MetadataKeyIsBinary))
               .ToLocalChecked());
  Nan::Set(exports, Nan::New("setDefaultRootsPem").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(SetDefaultRootsPem))
               .ToLocalChecked());
  Nan::Set(
      exports, Nan::New("setDefaultLoggerCallback").ToLocalChecked(),
      Nan::GetFunction(Nan::New<FunctionTemplate>(SetDefaultLoggerCallback))
          .ToLocalChecked());
  Nan::Set(exports, Nan::New("setLogVerbosity").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(SetLogVerbosity))
               .ToLocalChecked());
}

NODE_MODULE(grpc_node, init)
