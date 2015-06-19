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
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Channel option specified when creating a channel.
    /// Corresponds to grpc_channel_args from grpc/grpc.h.
    /// </summary>
    public sealed class ChannelOption
    {
        public enum OptionType
        {
            Integer,
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
            this.name = Preconditions.CheckNotNull(name);
            this.stringValue = Preconditions.CheckNotNull(stringValue);
        }

        /// <summary>
        /// Creates a channel option with an integer value.
        /// </summary>
        /// <param name="name">Name.</param>
        /// <param name="stringValue">String value.</param>
        public ChannelOption(string name, int intValue)
        {
            this.type = OptionType.Integer;
            this.name = Preconditions.CheckNotNull(name);
            this.intValue = intValue;
        }

        public OptionType Type
        {
            get
            {
                return type;
            }
        }

        public string Name
        {
            get
            {
                return name;
            }    
        }

        public int IntValue
        {
            get
            {
                Preconditions.CheckState(type == OptionType.Integer);
                return intValue;
            }
        }

        public string StringValue
        {
            get
            {
                Preconditions.CheckState(type == OptionType.String);
                return stringValue;
            }
        }
    }

    public static class ChannelOptions
    {
        // Override SSL target check. Only to be used for testing.
        public const string SslTargetNameOverride = "grpc.ssl_target_name_override";

        // Enable census for tracing and stats collection
        public const string Census = "grpc.census";

        // Maximum number of concurrent incoming streams to allow on a http2 connection
        public const string MaxConcurrentStreams = "grpc.max_concurrent_streams";

        // Maximum message length that the channel can receive
        public const string MaxMessageLength = "grpc.max_message_length";

        // Initial sequence number for http2 transports
        public const string Http2InitialSequenceNumber = "grpc.http2.initial_sequence_number";

        /// <summary>
        /// Creates native object for a collection of channel options.
        /// </summary>
        /// <returns>The native channel arguments.</returns>
        internal static ChannelArgsSafeHandle CreateChannelArgs(IEnumerable<ChannelOption> options)
        {
            if (options == null)
            {
                return ChannelArgsSafeHandle.CreateNull();
            }
            var optionList = new List<ChannelOption>(options);  // It's better to do defensive copy
            ChannelArgsSafeHandle nativeArgs = null;
            try
            {
                nativeArgs = ChannelArgsSafeHandle.Create(optionList.Count);
                for (int i = 0; i < optionList.Count; i++)
                {
                    var option = optionList[i];
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
