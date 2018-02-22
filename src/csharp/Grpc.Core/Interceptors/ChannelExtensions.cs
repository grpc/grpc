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

namespace Grpc.Core.Interceptors
{
    /// <summary>
    /// Provides extension methods to make it easy to register interceptors on Channel objects.
    /// This is an EXPERIMENTAL API.
    /// </summary>
    public static class ChannelExtensions
    {
        /// <summary>
        /// Returns a <see cref="Grpc.Core.CallInvoker" /> instance that intercepts
        /// the channel with the given interceptor.
        /// </summary>
        /// <param name="channel">The channel to intercept.</param>
        /// <param name="interceptor">The interceptor to intercept the channel with.</param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "channel.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted channel, effectively
        /// building a chain like "channel.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this Channel channel, Interceptor interceptor)
        {
            return new DefaultCallInvoker(channel).Intercept(interceptor);
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.CallInvoker" /> instance that intercepts
        /// the channel with the given interceptors.
        /// </summary>
        /// <param name="channel">The channel to intercept.</param>
        /// <param name="interceptors">
        /// An array of interceptors to intercept the channel with.
        /// Control is passed to the interceptors in the order specified.
        /// </param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "channel.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted channel, effectively
        /// building a chain like "channel.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this Channel channel, params Interceptor[] interceptors)
        {
            return new DefaultCallInvoker(channel).Intercept(interceptors);
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.CallInvoker" /> instance that intercepts
        /// the invoker with the given interceptor.
        /// </summary>
        /// <param name="channel">The channel to intercept.</param>
        /// <param name="interceptor">
        /// An interceptor delegate that takes the request metadata to be sent with an outgoing call
        /// and returns a <see cref="Grpc.Core.Metadata" /> instance that will replace the existing
        /// invocation metadata.
        /// </param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by
        /// building a chain like "channel.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this Channel channel, Func<Metadata, Metadata> interceptor)
        {
            return new DefaultCallInvoker(channel).Intercept(interceptor);
        }
    }
}
