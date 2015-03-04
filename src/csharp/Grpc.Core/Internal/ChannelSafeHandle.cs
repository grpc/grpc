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
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_channel from <grpc/grpc.h>
    /// </summary>
    internal class ChannelSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern ChannelSafeHandle grpcsharp_channel_create(string target, ChannelArgsSafeHandle channelArgs);

        [DllImport("grpc_csharp_ext.dll")]
        static extern ChannelSafeHandle grpcsharp_secure_channel_create(CredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_channel_destroy(IntPtr channel);

        private ChannelSafeHandle()
        {
        }

        public static ChannelSafeHandle Create(string target, ChannelArgsSafeHandle channelArgs)
        {
            return grpcsharp_channel_create(target, channelArgs);
        }

        public static ChannelSafeHandle CreateSecure(CredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs)
        {
            return grpcsharp_secure_channel_create(credentials, target, channelArgs);
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_channel_destroy(handle);
            return true;
        }
    }
}
