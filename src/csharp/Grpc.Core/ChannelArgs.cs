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
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// gRPC channel options.
    /// </summary>
    public class ChannelArgs
    {
        public const string SslTargetNameOverrideKey = "grpc.ssl_target_name_override";

        readonly ImmutableDictionary<string, string> stringArgs;

        private ChannelArgs(ImmutableDictionary<string, string> stringArgs)
        {
            this.stringArgs = stringArgs;
        }

        public string GetSslTargetNameOverride()
        {
            string result;
            if (stringArgs.TryGetValue(SslTargetNameOverrideKey, out result))
            {
                return result;
            }
            return null;
        }

        public static Builder CreateBuilder()
        {
            return new Builder();
        }

        public class Builder
        {
            readonly Dictionary<string, string> stringArgs = new Dictionary<string, string>();

            // TODO: AddInteger not supported yet.
            public Builder AddString(string key, string value)
            {
                stringArgs.Add(key, value);
                return this;
            }

            public ChannelArgs Build()
            {
                return new ChannelArgs(stringArgs.ToImmutableDictionary());
            }
        }

        /// <summary>
        /// Creates native object for the channel arguments.
        /// </summary>
        /// <returns>The native channel arguments.</returns>
        internal ChannelArgsSafeHandle ToNativeChannelArgs()
        {
            ChannelArgsSafeHandle nativeArgs = null;
            try
            {
                nativeArgs = ChannelArgsSafeHandle.Create(stringArgs.Count);
                int i = 0;
                foreach (var entry in stringArgs)
                {
                    nativeArgs.SetString(i, entry.Key, entry.Value);
                    i++;
                }
                return nativeArgs;
            }
            catch (Exception)
            {
                if (nativeArgs != null)
                {
                    nativeArgs.Dispose();
                }
                throw;
            }
        }
    }
}
