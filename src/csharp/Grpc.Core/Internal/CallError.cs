#region Copyright notice and license

// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#endregion

using System;
using System.Runtime.InteropServices;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_call_error from grpc/grpc.h
    /// </summary>
    internal enum CallError
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
        public static void CheckOk(this CallError callError)
        {
            if (callError != CallError.OK)
            {
                throw new InvalidOperationException("Call error: " + callError);
            }
        }
    }
}
