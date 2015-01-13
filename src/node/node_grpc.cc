#include <node.h>
#include <nan.h>
#include <v8.h>
#include "grpc/grpc.h"

#include "call.h"
#include "channel.h"
#include "event.h"
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
  Handle<Object> status = Object::New();
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
  Handle<Object> call_error = Object::New();
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
  call_error->Set(NanNew("TOO_MANY_OPERATIONS"),
                  TOO_MANY_OPERATIONS);
  Handle<Value> INVALID_FLAGS(
      NanNew<Uint32, uint32_t>(GRPC_CALL_ERROR_INVALID_FLAGS));
  call_error->Set(NanNew("INVALID_FLAGS"), INVALID_FLAGS);
}

void InitOpErrorConstants(Handle<Object> exports) {
  NanScope();
  Handle<Object> op_error = Object::New();
  exports->Set(NanNew("opError"), op_error);
  Handle<Value> OK(NanNew<Uint32, uint32_t>(GRPC_OP_OK));
  op_error->Set(NanNew("OK"), OK);
  Handle<Value> ERROR(NanNew<Uint32, uint32_t>(GRPC_OP_ERROR));
  op_error->Set(NanNew("ERROR"), ERROR);
}

void InitCompletionTypeConstants(Handle<Object> exports) {
  NanScope();
  Handle<Object> completion_type = Object::New();
  exports->Set(NanNew("completionType"), completion_type);
  Handle<Value> QUEUE_SHUTDOWN(NanNew<Uint32, uint32_t>(GRPC_QUEUE_SHUTDOWN));
  completion_type->Set(NanNew("QUEUE_SHUTDOWN"), QUEUE_SHUTDOWN);
  Handle<Value> READ(NanNew<Uint32, uint32_t>(GRPC_READ));
  completion_type->Set(NanNew("READ"), READ);
  Handle<Value> INVOKE_ACCEPTED(NanNew<Uint32, uint32_t>(GRPC_INVOKE_ACCEPTED));
  completion_type->Set(NanNew("INVOKE_ACCEPTED"), INVOKE_ACCEPTED);
  Handle<Value> WRITE_ACCEPTED(NanNew<Uint32, uint32_t>(GRPC_WRITE_ACCEPTED));
  completion_type->Set(NanNew("WRITE_ACCEPTED"), WRITE_ACCEPTED);
  Handle<Value> FINISH_ACCEPTED(NanNew<Uint32, uint32_t>(GRPC_FINISH_ACCEPTED));
  completion_type->Set(NanNew("FINISH_ACCEPTED"), FINISH_ACCEPTED);
  Handle<Value> CLIENT_METADATA_READ(
      NanNew<Uint32, uint32_t>(GRPC_CLIENT_METADATA_READ));
  completion_type->Set(NanNew("CLIENT_METADATA_READ"),
                       CLIENT_METADATA_READ);
  Handle<Value> FINISHED(NanNew<Uint32, uint32_t>(GRPC_FINISHED));
  completion_type->Set(NanNew("FINISHED"), FINISHED);
  Handle<Value> SERVER_RPC_NEW(NanNew<Uint32, uint32_t>(GRPC_SERVER_RPC_NEW));
  completion_type->Set(NanNew("SERVER_RPC_NEW"), SERVER_RPC_NEW);
}

void init(Handle<Object> exports) {
  NanScope();
  grpc_init();
  InitStatusConstants(exports);
  InitCallErrorConstants(exports);
  InitOpErrorConstants(exports);
  InitCompletionTypeConstants(exports);

  grpc::node::Call::Init(exports);
  grpc::node::Channel::Init(exports);
  grpc::node::Server::Init(exports);
  grpc::node::CompletionQueueAsyncWorker::Init(exports);
  grpc::node::Credentials::Init(exports);
  grpc::node::ServerCredentials::Init(exports);
}

NODE_MODULE(grpc, init)
