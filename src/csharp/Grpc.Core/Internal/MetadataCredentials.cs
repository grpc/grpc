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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Client-side credentials that delegate metadata based auth to an interceptor.
    /// The interceptor is automatically invoked for each remote call that uses <c>MetadataCredentials.</c>
    /// </summary>
    internal sealed class MetadataCredentials : CallCredentials
    {
        readonly AsyncAuthInterceptor interceptor;

        /// <summary>
        /// Initializes a new instance of <c>MetadataCredentials</c> class.
        /// </summary>
        /// <param name="interceptor">authentication interceptor</param>
        public MetadataCredentials(AsyncAuthInterceptor interceptor)
        {
            this.interceptor = GrpcPreconditions.CheckNotNull(interceptor);
        }

        internal override CallCredentialsSafeHandle ToNativeCredentials()
        {
            NativeMetadataCredentialsPlugin plugin = new NativeMetadataCredentialsPlugin(interceptor);
            return plugin.Credentials;
        }
    }
}
