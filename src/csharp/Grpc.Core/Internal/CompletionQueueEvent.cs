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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_event from grpc/grpc.h
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct CompletionQueueEvent
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        public CompletionType type;
        public int success;
        public IntPtr tag;

        internal static int NativeSize
        {
            get
            {
                return Native.grpcsharp_sizeof_grpc_event();
            }
        }

        /// <summary>
        /// grpc_completion_type from grpc/grpc.h
        /// </summary>
        internal enum CompletionType
        {
            /* Shutting down */
            Shutdown, 

            /* No event before timeout */
            Timeout,  

            /* operation completion */
            OpComplete
        }
    }
}
