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
    /// Commonly used channel option names are defined in <c>ChannelOptions</c>,
    /// but any of the GRPC_ARG_* channel options names defined in grpc_types.h can be used.
    /// </summary>
    public sealed class ChannelOption : IEquatable<ChannelOption>
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

        /// <summary>
        /// Determines whether the specified object is equal to the current object.
        /// </summary>
        public override bool Equals(object obj)
        {
            return Equals(obj as ChannelOption);
        }

        /// <summary>
        /// Determines whether the specified object is equal to the current object.
        /// </summary>
        public bool Equals(ChannelOption other)
        {
            return other != null &&
                   type == other.type &&
                   name == other.name &&
                   intValue == other.intValue &&
                   stringValue == other.stringValue;
        }

        /// <summary>
        /// A hash code for the current object.
        /// </summary>
        public override int GetHashCode()
        {
            var hashCode = 1412678443;
            hashCode = hashCode * -1521134295 + type.GetHashCode();
            hashCode = hashCode * -1521134295 + EqualityComparer<string>.Default.GetHashCode(name);
            hashCode = hashCode * -1521134295 + intValue.GetHashCode();
            hashCode = hashCode * -1521134295 + EqualityComparer<string>.Default.GetHashCode(stringValue);
            return hashCode;
        }

        /// <summary>
        /// Equality operator.
        /// </summary>
        public static bool operator ==(ChannelOption option1, ChannelOption option2)
        {
            return EqualityComparer<ChannelOption>.Default.Equals(option1, option2);
        }

        /// <summary>
        /// Inequality operator.
        /// </summary>
        public static bool operator !=(ChannelOption option1, ChannelOption option2)
        {
            return !(option1 == option2);
        }
    }

    /// <summary>
    /// Defines names of most commonly used channel options.
    /// Other supported options names can be found in grpc_types.h (GRPC_ARG_* definitions)
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
        public const string MaxReceiveMessageLength = "grpc.max_receive_message_length";

        /// <summary>Maximum message length that the channel can send</summary>
        public const string MaxSendMessageLength = "grpc.max_send_message_length";

        /// <summary>Obsolete, for backward compatibility only.</summary>
        [Obsolete("Use MaxReceiveMessageLength instead.")]
        public const string MaxMessageLength = MaxReceiveMessageLength;

        /// <summary>Initial sequence number for http2 transports</summary>
        public const string Http2InitialSequenceNumber = "grpc.http2.initial_sequence_number";

        /// <summary>Default authority for calls.</summary>
        public const string DefaultAuthority = "grpc.default_authority";

        /// <summary>Primary user agent: goes at the start of the user-agent metadata</summary>
        public const string PrimaryUserAgentString = "grpc.primary_user_agent";

        /// <summary>Secondary user agent: goes at the end of the user-agent metadata</summary>
        public const string SecondaryUserAgentString = "grpc.secondary_user_agent";

        /// <summary>If non-zero, allow the use of SO_REUSEPORT for server if it's available (default 1)</summary>
        public const string SoReuseport = "grpc.so_reuseport";

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
