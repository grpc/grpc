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
using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// gRPC Channel
    /// </summary>
    public class Channel : IDisposable
    {
        readonly ChannelSafeHandle handle;
        readonly string target;

        /// <summary>
        /// Creates a channel.
        /// </summary>
        public Channel(string target, Credentials credentials = null, ChannelArgs channelArgs = null)
        {
            using (ChannelArgsSafeHandle nativeChannelArgs = CreateNativeChannelArgs(channelArgs))
            {
                if (credentials != null)
                {
                    using (CredentialsSafeHandle nativeCredentials = credentials.ToNativeCredentials())
                    {
                        this.handle = ChannelSafeHandle.CreateSecure(nativeCredentials, target, nativeChannelArgs);
                    }
                }
                else
                {
                    this.handle = ChannelSafeHandle.Create(target, nativeChannelArgs);
                }
            }
            this.target = GetOverridenTarget(target, channelArgs);
        }

        internal ChannelSafeHandle Handle
        {
            get
            {
                return this.handle;
            }
        }

        public string Target
        {
            get
            {
                return this.target;
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (handle != null && !handle.IsInvalid)
            {
                handle.Dispose();
            }
        }

        private static string GetOverridenTarget(string target, ChannelArgs args)
        {
            if (args != null && !string.IsNullOrEmpty(args.GetSslTargetNameOverride()))
            {
                return args.GetSslTargetNameOverride();
            }
            return target;
        }

        private static ChannelArgsSafeHandle CreateNativeChannelArgs(ChannelArgs args)
        {
            if (args == null)
            {
                return ChannelArgsSafeHandle.CreateNull();
            }
            return args.ToNativeChannelArgs();
        }
    }
}
