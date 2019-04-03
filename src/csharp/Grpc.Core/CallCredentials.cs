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
    /// Client-side call credentials. Provide authorization with per-call granularity.
    /// </summary>
    public abstract class CallCredentials
    {
        /// <summary>
        /// Composes multiple multiple <c>CallCredentials</c> objects into
        /// a single <c>CallCredentials</c> object.
        /// </summary>
        /// <param name="credentials">credentials to compose</param>
        /// <returns>The new <c>CompositeCallCredentials</c></returns>
        public static CallCredentials Compose(params CallCredentials[] credentials)
        {
            return new CompositeCallCredentials(credentials);
        }

        /// <summary>
        /// Creates a new instance of <c>CallCredentials</c> class from an
        /// interceptor that can attach metadata to outgoing calls.
        /// </summary>
        /// <param name="interceptor">authentication interceptor</param>
        public static CallCredentials FromInterceptor(AsyncAuthInterceptor interceptor)
        {
            return new MetadataCredentials(interceptor);
        }

        /// <summary>
        /// Creates native object for the credentials.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract CallCredentialsSafeHandle ToNativeCredentials();
    }
}
