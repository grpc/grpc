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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Client-side channel credentials. Used for creation of a secure channel.
    /// </summary>
    public abstract class ChannelCredentials
    {
        static readonly ChannelCredentials InsecureInstance = new InsecureCredentials();

        /// <summary>
        /// Creates a new instance of channel credentials
        /// </summary>
        public ChannelCredentials()
        {
        }

        /// <summary>
        /// Returns instance of credentials that provides no security and 
        /// will result in creating an unsecure channel with no encryption whatsoever.
        /// </summary>
        public static ChannelCredentials Insecure
        {
            get
            {
                return InsecureInstance;
            }
        }

        /// <summary>
        /// Creates a new instance of <c>ChannelCredentials</c> class by composing
        /// given channel credentials with call credentials.
        /// </summary>
        /// <param name="channelCredentials">Channel credentials.</param>
        /// <param name="callCredentials">Call credentials.</param>
        /// <returns>The new composite <c>ChannelCredentials</c></returns>
        public static ChannelCredentials Create(ChannelCredentials channelCredentials, CallCredentials callCredentials)
        {
            return new CompositeChannelCredentials(channelCredentials, callCredentials);
        }

        /// <summary>
        /// Populates channel credentials configurator with this instance's configuration.
        /// End users never need to invoke this method as it is part of internal implementation.
        /// </summary>
        public abstract void InternalPopulateConfiguration(ChannelCredentialsConfiguratorBase configurator, object state);

        /// <summary>
        /// Returns <c>true</c> if this credential type allows being composed by <c>CompositeCredentials</c>.
        /// </summary>
        internal virtual bool IsComposable => false;

        private sealed class InsecureCredentials : ChannelCredentials
        {
            public override void InternalPopulateConfiguration(ChannelCredentialsConfiguratorBase configurator, object state)
            {
                configurator.SetInsecureCredentials(state);
            }
        }

        /// <summary>
        /// Credentials that allow composing one <see cref="ChannelCredentials"/> object and 
        /// one or more <see cref="CallCredentials"/> objects into a single <see cref="ChannelCredentials"/>.
        /// </summary>
        private sealed class CompositeChannelCredentials : ChannelCredentials
        {
            readonly ChannelCredentials channelCredentials;
            readonly CallCredentials callCredentials;

            /// <summary>
            /// Initializes a new instance of <c>CompositeChannelCredentials</c> class.
            /// The resulting credentials object will be composite of all the credentials specified as parameters.
            /// </summary>
            /// <param name="channelCredentials">channelCredentials to compose</param>
            /// <param name="callCredentials">channelCredentials to compose</param>
            public CompositeChannelCredentials(ChannelCredentials channelCredentials, CallCredentials callCredentials)
            {
                this.channelCredentials = GrpcPreconditions.CheckNotNull(channelCredentials);
                this.callCredentials = GrpcPreconditions.CheckNotNull(callCredentials);

                if (!channelCredentials.IsComposable)
                {
                    throw new ArgumentException(string.Format("CallCredentials can't be composed with {0}. CallCredentials must be used with secure channel credentials like SslCredentials.", channelCredentials.GetType().Name));
                }
            }

            public override void InternalPopulateConfiguration(ChannelCredentialsConfiguratorBase configurator, object state)
            {
                configurator.SetCompositeCredentials(state, channelCredentials, callCredentials);
            }
        }
    }
}
