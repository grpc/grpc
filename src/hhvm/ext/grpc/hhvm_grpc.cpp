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

#include "hphp/runtime/ext/extension.h"

#include "call.h"
#include "channel.h"
#include "server.h"
#include "timeval.h"
#include "channel_credentials.h"
#include "call_credentials.h"
#include "server_credentials.h"
#include "completion_queue.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

namespace HPHP {

class GrpcExtension : public Extension {
  public:
    GrpcExtension() : Extension("grpc", HHVM_GRPC_VERSION) {}
    virtual void moduleInit() {
      HHVM_RCC_INT(Grpc, CALL_OK, GRPC_CALL_OK);
      HHVM_RCC_INT(Grpc, CALL_ERROR, GRPC_CALL_ERROR);
      HHVM_RCC_INT(Grpc, CALL_ERROR_NOT_ON_SERVER, GRPC_CALL_ERROR_NOT_ON_SERVER);
      HHVM_RCC_INT(Grpc, CALL_ERROR_NOT_ON_CLIENT, GRPC_CALL_ERROR_NOT_ON_CLIENT);
      HHVM_RCC_INT(Grpc, CALL_ERROR_ALREADY_INVOKED, GRPC_CALL_ERROR_ALREADY_INVOKED);
      HHVM_RCC_INT(Grpc, CALL_ERROR_NOT_INVOKED, GRPC_CALL_ERROR_NOT_INVOKED);
      HHVM_RCC_INT(Grpc, CALL_ERROR_ALREADY_FINISHED, GRPC_CALL_ERROR_ALREADY_FINISHED);
      HHVM_RCC_INT(Grpc, CALL_ERROR_TOO_MANY_OPERATIONS, GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
      HHVM_RCC_INT(Grpc, CALL_ERROR_INVALID_FLAGS, GRPC_CALL_ERROR_INVALID_FLAGS);

      HHVM_RCC_INT(Grpc, WRITE_BUFFER_HINT, GRPC_WRITE_BUFFER_HINT);
      HHVM_RCC_INT(Grpc, WRITE_NO_COMPRESS, GRPC_WRITE_NO_COMPRESS);

      HHVM_RCC_INT(Grpc, STATUS_OK, GRPC_STATUS_OK);
      HHVM_RCC_INT(Grpc, STATUS_CANCELLED, GRPC_STATUS_CANCELLED);
      HHVM_RCC_INT(Grpc, STATUS_UNKNOWN, GRPC_STATUS_UNKNOWN);
      HHVM_RCC_INT(Grpc, STATUS_INVALID_ARGUMENT, GRPC_STATUS_INVALID_ARGUMENT);
      HHVM_RCC_INT(Grpc, STATUS_DEADLINE_EXCEEDED, GRPC_STATUS_DEADLINE_EXCEEDED);
      HHVM_RCC_INT(Grpc, STATUS_NOT_FOUND, GRPC_STATUS_NOT_FOUND);
      HHVM_RCC_INT(Grpc, STATUS_ALREADY_EXISTS, GRPC_STATUS_ALREADY_EXISTS);
      HHVM_RCC_INT(Grpc, STATUS_PERMISSION_DENIED, GRPC_STATUS_PERMISSION_DENIED);
      HHVM_RCC_INT(Grpc, STATUS_UNAUTHENTICATED, GRPC_STATUS_UNAUTHENTICATED);
      HHVM_RCC_INT(Grpc, STATUS_RESOURCE_EXHAUSTED, GRPC_STATUS_RESOURCE_EXHAUSTED);
      HHVM_RCC_INT(Grpc, STATUS_FAILED_PRECONDITION, GRPC_STATUS_FAILED_PRECONDITION);
      HHVM_RCC_INT(Grpc, STATUS_ABORTED, GRPC_STATUS_ABORTED);
      HHVM_RCC_INT(Grpc, STATUS_OUT_OF_RANGE, GRPC_STATUS_OUT_OF_RANGE);
      HHVM_RCC_INT(Grpc, STATUS_UNIMPLEMENTED, GRPC_STATUS_UNIMPLEMENTED);
      HHVM_RCC_INT(Grpc, STATUS_INTERNAL, GRPC_STATUS_INTERNAL);
      HHVM_RCC_INT(Grpc, STATUS_UNAVAILABLE, GRPC_STATUS_UNAVAILABLE);
      HHVM_RCC_INT(Grpc, STATUS_DATA_LOSS, GRPC_STATUS_DATA_LOSS);

      HHVM_RCC_INT(Grpc, OP_SEND_INITIAL_METADATA, GRPC_OP_SEND_INITIAL_METADATA);
      HHVM_RCC_INT(Grpc, OP_SEND_MESSAGE, GRPC_OP_SEND_MESSAGE);
      HHVM_RCC_INT(Grpc, OP_SEND_CLOSE_FROM_CLIENT, GRPC_OP_SEND_CLOSE_FROM_CLIENT);
      HHVM_RCC_INT(Grpc, OP_SEND_STATUS_FROM_SERVER, GRPC_OP_SEND_STATUS_FROM_SERVER);
      HHVM_RCC_INT(Grpc, OP_RECV_INITIAL_METADATA, GRPC_OP_RECV_INITIAL_METADATA);
      HHVM_RCC_INT(Grpc, OP_RECV_MESSAGE, GRPC_OP_RECV_MESSAGE);
      HHVM_RCC_INT(Grpc, OP_RECV_STATUS_ON_CLIENT, GRPC_OP_RECV_STATUS_ON_CLIENT);
      HHVM_RCC_INT(Grpc, OP_RECV_CLOSE_ON_SERVER, GRPC_OP_RECV_CLOSE_ON_SERVER);

      HHVM_RCC_INT(Grpc, CHANNEL_IDLE, GRPC_CHANNEL_IDLE);
      HHVM_RCC_INT(Grpc, CHANNEL_CONNECTING, GRPC_CHANNEL_CONNECTING);
      HHVM_RCC_INT(Grpc, CHANNEL_READY, GRPC_CHANNEL_READY);
      HHVM_RCC_INT(Grpc, CHANNEL_TRANSIENT_FAILURE, GRPC_CHANNEL_TRANSIENT_FAILURE);
      HHVM_RCC_INT(Grpc, CHANNEL_FATAL_FAILURE, GRPC_CHANNEL_FATAL_FAILURE);

      HHVM_ME(Call, __construct);
      HHVM_ME(Call, startBatch);
      HHVM_ME(Call, getPeer);
      HHVM_ME(Call, cancel);
      HHVM_ME(Call, setCredentials);

      HHVM_ME(CallCredentials, createComposite);
      HHVM_ME(CallCredentials, createFromPlugin);

      HHVM_ME(Channel, __construct);
      HHVM_ME(Channel, getTarget);
      HHVM_ME(Channel, getConnectivityState);
      HHVM_ME(Channel, watchConnectivityState);
      HHVM_ME(Channel, close);

      HHVM_ME(ChannelCredentials, setDefaultRootsPem);
      HHVM_ME(ChannelCredentials, createDefault);
      HHVM_ME(ChannelCredentials, createSsl);
      HHVM_ME(ChannelCredentials, createComposite);
      HHVM_ME(ChannelCredentials, createInsecure);

      HHVM_ME(Server, __construct);
      HHVM_ME(Server, requestCall);
      HHVM_ME(Server, addHttp2Port);
      HHVM_ME(Server, addSecureHttp2Port);
      HHVM_ME(Server, start);

      HHVM_ME(ServerCredentials, createSsl);

      HHVM_ME(Timeval, __construct);
      HHVM_ME(Timeval, add);
      HHVM_ME(Timeval, subtract);
      HHVM_STATIC_ME(Timeval, compare);
      HHVM_STATIC_ME(Timeval, similar);
      HHVM_STATIC_ME(Timeval, now);
      HHVM_STATIC_ME(Timeval, zero);
      HHVM_STATIC_ME(Timeval, infFuture);
      HHVM_STATIC_ME(Timeval, infPast);
      HHVM_ME(Timeval, sleepUntil);

      Native::registerNativeDataInfo<Timeval>(s_TimevalWrapper.get());
      Native::registerNativeDataInfo<ServerCredentials>(s_ServerCredentialsWrapper.get());
      Native::registerNativeDataInfo<Server>(s_ServerWrapper.get());
      Native::registerNativeDataInfo<ChannelCredentials>(s_ChannelCredentialsWrapper.get());

      loadSystemlib()

      /* Register call error constants */
      grpc_init();

      grpc_init_call();
      grpc_init_channel();
      grpc_init_server();
      grpc_init_timeval();
      grpc_init_channel_credentials();
      grpc_init_call_credentials();
      grpc_init_server_credentials();
      grpc_php_init_completion_queue();
    }

} s_grpc_extension;

HHVM_GET_MODULE(grpc);

}
