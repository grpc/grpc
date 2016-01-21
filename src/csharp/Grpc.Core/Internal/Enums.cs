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
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_call_error from grpc/grpc.h
    /// </summary>
    internal enum GRPCCallError
    {
        /* everything went ok */
        OK = 0,
        /* something failed, we don't know what */
        Error,
        /* this method is not available on the server */
        NotOnServer,
        /* this method is not available on the client */
        NotOnClient,
        /* this method must be called before server_accept */
        AlreadyAccepted,
        /* this method must be called before invoke */
        AlreadyInvoked,
        /* this method must be called after invoke */
        NotInvoked,
        /* this call is already finished
     (writes_done or write_status has already been called) */
        AlreadyFinished,
        /* there is already an outstanding read/write operation on the call */
        TooManyOperations,
        /* the flags value was illegal for this call */
        InvalidFlags
    }

    internal static class CallErrorExtensions
    {
        /// <summary>
        /// Checks the call API invocation's result is OK.
        /// </summary>
        public static void CheckOk(this GRPCCallError callError)
        {
            Preconditions.CheckState(callError == GRPCCallError.OK, "Call error: " + callError);
        }
    }

    /// <summary>
    /// grpc_completion_type from grpc/grpc.h
    /// </summary>
    internal enum GRPCCompletionType
    {
        /* Shutting down */
        Shutdown, 

        /* No event before timeout */
        Timeout,  

        /* operation completion */
        OpComplete
    }

    /// <summary>
    /// gpr_clock_type from grpc/support/time.h
    /// </summary>
    internal enum GPRClockType
    {
        /* Monotonic clock */
        Monotonic,

        /* Realtime clock */
        Realtime,

        /* Precise clock good for performance profiling. */
        Precise,

        /* Timespan - the distance between two time points */
        Timespan
    }
}
