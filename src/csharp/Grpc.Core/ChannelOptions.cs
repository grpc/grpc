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
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Channel option specified when creating a channel.
    /// Corresponds to grpc_channel_args from grpc/grpc.h.
    /// </summary>
    public sealed class ChannelOption
    {
        /// <summary>
        /// Type of <c>ChannelOption</c>.
        /// </summary>
        public enum OptionType
        {
            /// <summary>
            /// Channel option with integer value.
            /// </summary>
            Integer,
            
            /// <summary>
            /// Channel option with string value.
            /// </summary>
            String
        }

        private readonly OptionType type;
        private readonly string name;
        private readonly int intValue;
        private readonly string stringValue;

        /// <summary>
        /// Creates a channel option with a string value.
        /// </summary>
        /// <param name="name">Name.</param>
        /// <param name="stringValue">String value.</param>
        public ChannelOption(string name, string stringValue)
        {
            this.type = OptionType.String;
            this.name = GrpcPreconditions.CheckNotNull(name, "name");
            this.stringValue = GrpcPreconditions.CheckNotNull(stringValue, "stringValue");
        }

        /// <summary>
        /// Creates a channel option with an integer value.
        /// </summary>
        /// <param name="name">Name.</param>
        /// <param name="intValue">Integer value.</param>
        public ChannelOption(string name, int intValue)
        {
            this.type = OptionType.Integer;
            this.name = GrpcPreconditions.CheckNotNull(name, "name");
            this.intValue = intValue;
        }

        /// <summary>
        /// Gets the type of the <c>ChannelOption</c>.
        /// </summary>
        public OptionType Type
        {
            get
            {
                return type;
            }
        }

        /// <summary>
        /// Gets the name of the <c>ChannelOption</c>.
        /// </summary>
        public string Name
        {
            get
            {
                return name;
            }    
        }

        /// <summary>
        /// Gets the integer value the <c>ChannelOption</c>.
        /// </summary>
        public int IntValue
        {
            get
            {
                GrpcPreconditions.CheckState(type == OptionType.Integer);
                return intValue;
            }
        }

        /// <summary>
        /// Gets the string value the <c>ChannelOption</c>.
        /// </summary>
        public string StringValue
        {
            get
            {
                GrpcPreconditions.CheckState(type == OptionType.String);
                return stringValue;
            }
        }
    }

    /// <summary>
    /// Defines names of supported channel options.
    /// </summary>
    public static class ChannelOptions
    {
        /// <summary>Override SSL target check. Only to be used for testing.</summary>
        public const string SslTargetNameOverride = "grpc.ssl_target_name_override";

        /// <summary>Enable census for tracing and stats collection</summary>
        public const string Census = "grpc.census";

        /// <summary>Maximum number of concurrent incoming streams to allow on a http2 connection</summary>
        public const string MaxConcurrentStreams = "grpc.max_concurrent_streams";

        /// <summary>Maximum message length that the channel can receive</summary>
        public const string MaxMessageLength = "grpc.max_message_length";

        /// <summary>Initial sequence number for http2 transports</summary>
        public const string Http2InitialSequenceNumber = "grpc.http2.initial_sequence_number";

        /// <summary>Default authority for calls.</summary>
        public const string DefaultAuthority = "grpc.default_authority";

        /// <summary>Primary user agent: goes at the start of the user-agent metadata</summary>
        public const string PrimaryUserAgentString = "grpc.primary_user_agent";

        /// <summary>Secondary user agent: goes at the end of the user-agent metadata</summary>
        public const string SecondaryUserAgentString = "grpc.secondary_user_agent";

        /// <summary>
        /// Creates native object for a collection of channel options.
        /// </summary>
        /// <returns>The native channel arguments.</returns>
        internal static ChannelArgsSafeHandle CreateChannelArgs(ICollection<ChannelOption> options)
        {
            if (options == null || options.Count == 0)
            {
                return ChannelArgsSafeHandle.CreateNull();
            }
            ChannelArgsSafeHandle nativeArgs = null;
            try
            {
                nativeArgs = ChannelArgsSafeHandle.Create(options.Count);
                int i = 0;
                foreach (var option in options)
                {
                    if (option.Type == ChannelOption.OptionType.Integer)
                    {
                        nativeArgs.SetInteger(i, option.Name, option.IntValue);
                    }
                    else if (option.Type == ChannelOption.OptionType.String)
                    {
                        nativeArgs.SetString(i, option.Name, option.StringValue);
                    }
                    else 
                    {
                        throw new InvalidOperationException("Unknown option type");
                    }
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
