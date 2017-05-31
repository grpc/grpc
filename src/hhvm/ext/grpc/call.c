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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "call.h"

#include "hphp/runtime/ext/extension.h"

#include "hhvm_grpc.h"

namespace HPHP {

/**
 * Constructs a new instance of the Call class.
 * @param Channel $channel_obj The channel to associate the call with.
 *                             Must not be closed.
 * @param string $method The method to call
 * @param Timeval $deadline_obj The deadline for completing the call
 * @param string $host_override The host is set by user (optional)
 */
void HHVM_METHOD(Call, __construct,
  Channel& channel_obj,
  const String& method,
  Timeval& deadline_obj,
  const Variant& host_override /* = null */) {
  ...
}

/**
 * Start a batch of RPC actions.
 * @param array $array Array of actions to take
 * @return object Object with results of all actions
 */
Object HHVM_METHOD(Call, startBatch,
  Array result) {
  ...
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
String HHVM_METHOD(Call, getPeer) {
  ...
}

/**
 * Cancel the call. This will cause the call to end with STATUS_CANCELLED
 * if it has not already ended with another status.
 * @return void
 */
void HHVM_METHOD(Call, cancel) {
  ...
}

/**
 * Set the CallCredentials for this call.
 * @param CallCredentials $creds_obj The CallCredentials object
 * @return int The error code
 */
int HHVM_METHOD(Call, setCredentials,
  CallCredentials& creds_obj) {
  ...
}

}
