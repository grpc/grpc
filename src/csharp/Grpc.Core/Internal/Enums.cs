#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;
using System.Runtime.InteropServices;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// from grpc/grpc.h
    /// </summary>
    internal enum GRPCCallError
    {
        /* everything went ok */
        GRPC_CALL_OK = 0,
        /* something failed, we don't know what */
        GRPC_CALL_ERROR,
        /* this method is not available on the server */
        GRPC_CALL_ERROR_NOT_ON_SERVER,
        /* this method is not available on the client */
        GRPC_CALL_ERROR_NOT_ON_CLIENT,
        /* this method must be called before server_accept */
        GRPC_CALL_ERROR_ALREADY_ACCEPTED,
        /* this method must be called before invoke */
        GRPC_CALL_ERROR_ALREADY_INVOKED,
        /* this method must be called after invoke */
        GRPC_CALL_ERROR_NOT_INVOKED,
        /* this call is already finished
     (writes_done or write_status has already been called) */
        GRPC_CALL_ERROR_ALREADY_FINISHED,
        /* there is already an outstanding read/write operation on the call */
        GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
        /* the flags value was illegal for this call */
        GRPC_CALL_ERROR_INVALID_FLAGS
    }

    /// <summary>
    /// grpc_completion_type from grpc/grpc.h
    /// </summary>
    internal enum GRPCCompletionType
    {
        /* Shutting down */
        GRPC_QUEUE_SHUTDOWN, 

        /* No event before timeout */
        GRPC_QUEUE_TIMEOUT,  

        /* operation completion */
        GRPC_OP_COMPLETE
    }
}
