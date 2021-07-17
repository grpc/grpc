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

using System.Threading;
using System.Threading.Tasks;

using Google.Apis.Auth.OAuth2;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Auth
{
    /// <summary>
    /// Factory methods to create authorization interceptors for Google credentials.
    /// <seealso cref="GoogleGrpcCredentials"/>
    /// </summary>
    public static class GoogleAuthInterceptors
    {
        private const string AuthorizationHeader = "Authorization";
        private const string Schema = "Bearer";

        /// <summary>
        /// Creates an <see cref="AsyncAuthInterceptor"/> that will obtain access token from any credential type that implements
        /// <c>ITokenAccess</c>. (e.g. <c>GoogleCredential</c>).
        /// </summary>
        /// <param name="credential">The credential to use to obtain access tokens.</param>
        /// <returns>The interceptor.</returns>
        public static AsyncAuthInterceptor FromCredential(ITokenAccess credential)
        {
            if (credential is ITokenAccessWithHeaders credentialWithHeaders)
            {
                return FromCredential(credentialWithHeaders);
            }

            return new AsyncAuthInterceptor(async (context, metadata) =>
            {
                var accessToken = await credential.GetAccessTokenForRequestAsync(context.ServiceUrl, CancellationToken.None).ConfigureAwait(false);
                metadata.Add(CreateBearerTokenHeader(accessToken));
            });
        }

        /// <summary>
        /// Creates an <see cref="AsyncAuthInterceptor"/> that will obtain access token and associated information
        /// from any credential type that implements <see cref="ITokenAccessWithHeaders"/>
        /// </summary>
        /// <param name="credential">The credential to use to obtain access tokens.</param>
        /// <returns>The interceptor.</returns>
        public static AsyncAuthInterceptor FromCredential(ITokenAccessWithHeaders credential)
        {
            return new AsyncAuthInterceptor(async (context, metadata) => 
            {
                AccessTokenWithHeaders tokenAndHeaders = await credential.GetAccessTokenWithHeadersForRequestAsync(context.ServiceUrl, CancellationToken.None).ConfigureAwait(false);
                metadata.Add(CreateBearerTokenHeader(tokenAndHeaders.AccessToken));
                foreach (var header in tokenAndHeaders.Headers)
                {
                    foreach (var headerValue in header.Value)
                    {
                        metadata.Add(new Metadata.Entry(header.Key, headerValue));
                    }
                }
            });
        }

        /// <summary>
        /// Creates an <see cref="AsyncAuthInterceptor"/> that will use given access token as authorization.
        /// </summary>
        /// <param name="accessToken">OAuth2 access token.</param>
        /// <returns>The interceptor.</returns>
        public static AsyncAuthInterceptor FromAccessToken(string accessToken)
        {
            GrpcPreconditions.CheckNotNull(accessToken);
            return new AsyncAuthInterceptor((context, metadata) =>
            {
                metadata.Add(CreateBearerTokenHeader(accessToken));
                return GetCompletedTask();
            });
        }

        private static Metadata.Entry CreateBearerTokenHeader(string accessToken)
        {
            return new Metadata.Entry(AuthorizationHeader, Schema + " " + accessToken);
        }

        /// <summary>
        /// Framework independent equivalent of <c>Task.CompletedTask</c>.
        /// </summary>
        private static Task GetCompletedTask()
        {
#if NETSTANDARD
            return Task.CompletedTask;
#else
            return Task.FromResult<object>(null);  // for .NET45, emulate the functionality
#endif
        }
    }
}
