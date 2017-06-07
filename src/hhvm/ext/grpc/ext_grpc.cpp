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
#include "hphp/runtime/vm/native-data.h"

#include "call.h"
#include "channel.h"
#include "server.h"
#include "timeval.h"
#include "channel_credentials.h"
#include "call_credentials.h"
#include "server_credentials.h"
#include "completion_queue.h"
#include "version.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace HPHP;

namespace HPHP {

const StaticString s_Grpc("Grpc");
class Grpc {
 public:
  Grpc() { }
  ~Grpc() { sweep(); }

  void sweep() { }
};

class GrpcExtension : public Extension {
  public:
    GrpcExtension() : Extension("grpc", HHVM_GRPC_VERSION) {}
    virtual void moduleInit() {
      HHVM_RC_INT(Grpc\\CALL_OK, GRPC_CALL_OK);
      HHVM_RC_INT(Grpc\\CALL_ERROR, GRPC_CALL_ERROR);
      HHVM_RC_INT(Grpc\\CALL_ERROR_NOT_ON_SERVER, GRPC_CALL_ERROR_NOT_ON_SERVER);
      HHVM_RC_INT(Grpc\\CALL_ERROR_NOT_ON_CLIENT, GRPC_CALL_ERROR_NOT_ON_CLIENT);
      HHVM_RC_INT(Grpc\\CALL_ERROR_ALREADY_INVOKED, GRPC_CALL_ERROR_ALREADY_INVOKED);
      HHVM_RC_INT(Grpc\\CALL_ERROR_NOT_INVOKED, GRPC_CALL_ERROR_NOT_INVOKED);
      HHVM_RC_INT(Grpc\\CALL_ERROR_ALREADY_FINISHED, GRPC_CALL_ERROR_ALREADY_FINISHED);
      HHVM_RC_INT(Grpc\\CALL_ERROR_TOO_MANY_OPERATIONS, GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
      HHVM_RC_INT(Grpc\\CALL_ERROR_INVALID_FLAGS, GRPC_CALL_ERROR_INVALID_FLAGS);

      HHVM_RC_INT(Grpc\\WRITE_BUFFER_HINT, GRPC_WRITE_BUFFER_HINT);
      HHVM_RC_INT(Grpc\\WRITE_NO_COMPRESS, GRPC_WRITE_NO_COMPRESS);

      HHVM_RC_INT(Grpc\\STATUS_OK, GRPC_STATUS_OK);
      HHVM_RC_INT(Grpc\\STATUS_CANCELLED, GRPC_STATUS_CANCELLED);
      HHVM_RC_INT(Grpc\\STATUS_UNKNOWN, GRPC_STATUS_UNKNOWN);
      HHVM_RC_INT(Grpc\\STATUS_INVALID_ARGUMENT, GRPC_STATUS_INVALID_ARGUMENT);
      HHVM_RC_INT(Grpc\\STATUS_DEADLINE_EXCEEDED, GRPC_STATUS_DEADLINE_EXCEEDED);
      HHVM_RC_INT(Grpc\\STATUS_NOT_FOUND, GRPC_STATUS_NOT_FOUND);
      HHVM_RC_INT(Grpc\\STATUS_ALREADY_EXISTS, GRPC_STATUS_ALREADY_EXISTS);
      HHVM_RC_INT(Grpc\\STATUS_PERMISSION_DENIED, GRPC_STATUS_PERMISSION_DENIED);
      HHVM_RC_INT(Grpc\\STATUS_UNAUTHENTICATED, GRPC_STATUS_UNAUTHENTICATED);
      HHVM_RC_INT(Grpc\\STATUS_RESOURCE_EXHAUSTED, GRPC_STATUS_RESOURCE_EXHAUSTED);
      HHVM_RC_INT(Grpc\\STATUS_FAILED_PRECONDITION, GRPC_STATUS_FAILED_PRECONDITION);
      HHVM_RC_INT(Grpc\\STATUS_ABORTED, GRPC_STATUS_ABORTED);
      HHVM_RC_INT(Grpc\\STATUS_OUT_OF_RANGE, GRPC_STATUS_OUT_OF_RANGE);
      HHVM_RC_INT(Grpc\\STATUS_UNIMPLEMENTED, GRPC_STATUS_UNIMPLEMENTED);
      HHVM_RC_INT(Grpc\\STATUS_INTERNAL, GRPC_STATUS_INTERNAL);
      HHVM_RC_INT(Grpc\\STATUS_UNAVAILABLE, GRPC_STATUS_UNAVAILABLE);
      HHVM_RC_INT(Grpc\\STATUS_DATA_LOSS, GRPC_STATUS_DATA_LOSS);

      HHVM_RC_INT(Grpc\\OP_SEND_INITIAL_METADATA, GRPC_OP_SEND_INITIAL_METADATA);
      HHVM_RC_INT(Grpc\\OP_SEND_MESSAGE, GRPC_OP_SEND_MESSAGE);
      HHVM_RC_INT(Grpc\\OP_SEND_CLOSE_FROM_CLIENT, GRPC_OP_SEND_CLOSE_FROM_CLIENT);
      HHVM_RC_INT(Grpc\\OP_SEND_STATUS_FROM_SERVER, GRPC_OP_SEND_STATUS_FROM_SERVER);
      HHVM_RC_INT(Grpc\\OP_RECV_INITIAL_METADATA, GRPC_OP_RECV_INITIAL_METADATA);
      HHVM_RC_INT(Grpc\\OP_RECV_MESSAGE, GRPC_OP_RECV_MESSAGE);
      HHVM_RC_INT(Grpc\\OP_RECV_STATUS_ON_CLIENT, GRPC_OP_RECV_STATUS_ON_CLIENT);
      HHVM_RC_INT(Grpc\\OP_RECV_CLOSE_ON_SERVER, GRPC_OP_RECV_CLOSE_ON_SERVER);

      HHVM_RC_INT(Grpc\\CHANNEL_IDLE, GRPC_CHANNEL_IDLE);
      HHVM_RC_INT(Grpc\\CHANNEL_CONNECTING, GRPC_CHANNEL_CONNECTING);
      HHVM_RC_INT(Grpc\\CHANNEL_READY, GRPC_CHANNEL_READY);
      HHVM_RC_INT(Grpc\\CHANNEL_TRANSIENT_FAILURE, GRPC_CHANNEL_TRANSIENT_FAILURE);
      HHVM_RC_INT(Grpc\\CHANNEL_FATAL_FAILURE, GRPC_CHANNEL_SHUTDOWN);

      HHVM_MALIAS(Grpc\\Call, __construct, Call, __construct);
      HHVM_MALIAS(Grpc\\Call, startBatch, Call, startBatch);
      HHVM_MALIAS(Grpc\\Call, getPeer, Call, getPeer);
      HHVM_MALIAS(Grpc\\Call, cancel, Call, cancel);
      HHVM_MALIAS(Grpc\\Call, setCredentials, Call, setCredentials);

      HHVM_STATIC_MALIAS(Grpc\\CallCredentials, createComposite, CallCredentials, createComposite);
      HHVM_STATIC_MALIAS(Grpc\\CallCredentials, createFromPlugin, CallCredentials, createFromPlugin);

      HHVM_MALIAS(Grpc\\Channel, __construct, Channel, __construct);
      HHVM_MALIAS(Grpc\\Channel, getTarget, Channel, getTarget);
      HHVM_MALIAS(Grpc\\Channel, getConnectivityState, Channel, getConnectivityState);
      HHVM_MALIAS(Grpc\\Channel, watchConnectivityState, Channel, watchConnectivityState);
      HHVM_MALIAS(Grpc\\Channel, close, Channel, close);

      HHVM_STATIC_MALIAS(Grpc\\ChannelCredentials, setDefaultRootsPem, ChannelCredentials, setDefaultRootsPem);
      HHVM_STATIC_MALIAS(Grpc\\ChannelCredentials, createDefault, ChannelCredentials, createDefault);
      HHVM_STATIC_MALIAS(Grpc\\ChannelCredentials, createSsl, ChannelCredentials, createSsl);
      HHVM_STATIC_MALIAS(Grpc\\ChannelCredentials, createComposite, ChannelCredentials, createComposite);
      HHVM_STATIC_MALIAS(Grpc\\ChannelCredentials, createInsecure, ChannelCredentials, createInsecure);

      HHVM_MALIAS(Grpc\\Server, __construct, Server, __construct);
      HHVM_MALIAS(Grpc\\Server, requestCall, Server, requestCall);
      HHVM_MALIAS(Grpc\\Server, addHttp2Port, Server, addHttp2Port);
      HHVM_MALIAS(Grpc\\Server, addSecureHttp2Port, Server, addSecureHttp2Port);
      HHVM_MALIAS(Grpc\\Server, start, Server, start);

      HHVM_STATIC_MALIAS(Grpc\\ServerCredentials, createSsl, ServerCredentials, createSsl);

      HHVM_MALIAS(Grpc\\Timeval, __construct, Timeval, __construct);
      HHVM_MALIAS(Grpc\\Timeval, add, Timeval, add);
      HHVM_MALIAS(Grpc\\Timeval, subtract, Timeval, subtract);
      HHVM_STATIC_MALIAS(Grpc\\Timeval, compare, Timeval, compare);
      HHVM_STATIC_MALIAS(Grpc\\Timeval, similar, Timeval, similar);
      HHVM_STATIC_MALIAS(Grpc\\Timeval, now, Timeval, now);
      HHVM_STATIC_MALIAS(Grpc\\Timeval, zero, Timeval, zero);
      HHVM_STATIC_MALIAS(Grpc\\Timeval, infFuture, Timeval, infFuture);
      HHVM_STATIC_MALIAS(Grpc\\Timeval, infPast, Timeval, infPast);
      HHVM_MALIAS(Grpc\\Timeval, sleepUntil, Timeval, sleepUntil);

      Native::registerNativeDataInfo<TimevalData>(TimevalData::s_className.get());
      Native::registerNativeDataInfo<ServerCredentialsData>(ServerCredentialsData::s_className.get());
      Native::registerNativeDataInfo<ServerData>(ServerData::s_className.get());
      Native::registerNativeDataInfo<ChannelCredentialsData>(ChannelCredentialsData::s_className.get());
      Native::registerNativeDataInfo<ChannelData>(ChannelData::s_className.get());
      Native::registerNativeDataInfo<CallData>(CallData::s_className.get());

      /* Register call error constants */
      grpc_init();

      grpc_hhvm_init_completion_queue();

      loadSystemlib();
    }

} s_grpc_extension;

HHVM_GET_MODULE(grpc);

} // namespace HPHP
