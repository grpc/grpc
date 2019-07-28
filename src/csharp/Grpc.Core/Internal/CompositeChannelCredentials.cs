#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Credentials that allow composing one <see cref="ChannelCredentials"/> object and 
    /// one or more <see cref="CallCredentials"/> objects into a single <see cref="ChannelCredentials"/>.
    /// </summary>
    internal sealed class CompositeChannelCredentials : ChannelCredentials
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
            GrpcPreconditions.CheckArgument(channelCredentials.IsComposable, "Supplied channel credentials do not allow composition.");
        }

        internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
        {
            using (var callCreds = callCredentials.ToNativeCredentials())
            {
                var nativeComposite = ChannelCredentialsSafeHandle.CreateComposite(channelCredentials.GetNativeCredentials(), callCreds);
                if (nativeComposite.IsInvalid)
                {
                    throw new ArgumentException("Error creating native composite credentials. Likely, this is because you are trying to compose incompatible credentials.");
                }
                return nativeComposite;
            }
        }
    }
}
