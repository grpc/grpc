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
using Grpc.Core;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpcsharp_request_call_context
    /// </summary>
    internal class RequestCallContextSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private RequestCallContextSafeHandle()
        {
        }

        public static RequestCallContextSafeHandle Create()
        {
            return Native.grpcsharp_request_call_context_create();
        }

        public IntPtr Handle
        {
            get
            {
                return handle;
            }
        }

        // Gets data of server_rpc_new completion.
        public ServerRpcNew GetServerRpcNew(Server server)
        {
            var call = Native.grpcsharp_request_call_context_call(this);

            UIntPtr methodLen;
            IntPtr methodPtr = Native.grpcsharp_request_call_context_method(this, out methodLen);
            var method = Marshal.PtrToStringAnsi(methodPtr, (int) methodLen.ToUInt32());

            UIntPtr hostLen;
            IntPtr hostPtr = Native.grpcsharp_request_call_context_host(this, out hostLen);
            var host = Marshal.PtrToStringAnsi(hostPtr, (int) hostLen.ToUInt32());

            var deadline = Native.grpcsharp_request_call_context_deadline(this);

            IntPtr metadataArrayPtr = Native.grpcsharp_request_call_context_request_metadata(this);
            var metadata = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(metadataArrayPtr);

            return new ServerRpcNew(server, call, method, host, deadline, metadata);
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_request_call_context_destroy(handle);
            return true;
        }
    }
}
