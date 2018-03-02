#region Copyright notice and license

// Copyright 2018 gRPC authors.
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
using System.Linq;
using Grpc.Core.Utils;

namespace Grpc.Core.Interceptors
{
    /// <summary>
    /// Extends the ServerServiceDefinition class to add methods used to register interceptors on the server side.
    /// This is an EXPERIMENTAL API.
    /// </summary>
    public static class ServerServiceDefinitionExtensions
    {
        /// <summary>
        /// Returns a <see cref="Grpc.Core.ServerServiceDefinition" /> instance that
        /// intercepts incoming calls to the underlying service handler through the given interceptor.
        /// This is an EXPERIMENTAL API.
        /// </summary>
        /// <param name="serverServiceDefinition">The <see cref="Grpc.Core.ServerServiceDefinition" /> instance to register interceptors on.</param>
        /// <param name="interceptor">The interceptor to intercept the incoming invocations with.</param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "serverServiceDefinition.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted service definition, effectively
        /// building a chain like "serverServiceDefinition.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static ServerServiceDefinition Intercept(this ServerServiceDefinition serverServiceDefinition, Interceptor interceptor)
        {
            GrpcPreconditions.CheckNotNull(serverServiceDefinition, nameof(serverServiceDefinition));
            GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));
            return new ServerServiceDefinition(serverServiceDefinition.CallHandlers.ToDictionary(x => x.Key, x => x.Value.Intercept(interceptor)));
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.ServerServiceDefinition" /> instance that
        /// intercepts incoming calls to the underlying service handler through the given interceptors.
        /// This is an EXPERIMENTAL API.
        /// </summary>
        /// <param name="serverServiceDefinition">The <see cref="Grpc.Core.ServerServiceDefinition" /> instance to register interceptors on.</param>
        /// <param name="interceptors">
        /// An array of interceptors to intercept the incoming invocations with.
        /// Control is passed to the interceptors in the order specified.
        /// </param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "serverServiceDefinition.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted service definition, effectively
        /// building a chain like "serverServiceDefinition.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static ServerServiceDefinition Intercept(this ServerServiceDefinition serverServiceDefinition, params Interceptor[] interceptors)
        {
            GrpcPreconditions.CheckNotNull(serverServiceDefinition, nameof(serverServiceDefinition));
            GrpcPreconditions.CheckNotNull(interceptors, nameof(interceptors));

            foreach (var interceptor in interceptors.Reverse())
            {
                serverServiceDefinition = Intercept(serverServiceDefinition, interceptor);
            }

            return serverServiceDefinition;
        }
    }
}