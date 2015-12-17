
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
#ifndef GRPC++_CC_
#define GRPC++_CC_


#include "src/aggregate/gpr.c"
#include "src/aggregate/grpc.c"

#include "src/cpp/client/secure_credentials.cc"
#include "src/cpp/common/auth_property_iterator.cc"
#include "src/cpp/common/secure_auth_context.cc"
#include "src/cpp/common/secure_channel_arguments.cc"
#include "src/cpp/common/secure_create_auth_context.cc"
#include "src/cpp/server/secure_server_credentials.cc"
#include "src/cpp/client/channel.cc"
#include "src/cpp/client/client_context.cc"
#include "src/cpp/client/create_channel.cc"
#include "src/cpp/client/create_channel_internal.cc"
#include "src/cpp/client/credentials.cc"
#include "src/cpp/client/generic_stub.cc"
#include "src/cpp/client/insecure_credentials.cc"
#include "src/cpp/common/call.cc"
#include "src/cpp/common/channel_arguments.cc"
#include "src/cpp/common/completion_queue.cc"
#include "src/cpp/common/rpc_method.cc"
#include "src/cpp/proto/proto_utils.cc"
#include "src/cpp/server/async_generic_service.cc"
#include "src/cpp/server/create_default_thread_pool.cc"
#include "src/cpp/server/dynamic_thread_pool.cc"
#include "src/cpp/server/fixed_size_thread_pool.cc"
#include "src/cpp/server/insecure_server_credentials.cc"
#include "src/cpp/server/server.cc"
#include "src/cpp/server/server_builder.cc"
#include "src/cpp/server/server_context.cc"
#include "src/cpp/server/server_credentials.cc"
#include "src/cpp/util/byte_buffer.cc"
#include "src/cpp/util/slice.cc"
#include "src/cpp/util/status.cc"
#include "src/cpp/util/string_ref.cc"
#include "src/cpp/util/time.cc"


#endif /* GRPC++_CC_ */
