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
using System.Threading;

using Google.Apis.Auth.OAuth2;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Auth
{
    /// <summary>
    /// Factory methods to create authorization interceptors.
    /// </summary>
    public static class AuthInterceptors
    {
        private const string AuthorizationHeader = "Authorization";
        private const string Schema = "Bearer";

        /// <summary>
        /// Creates interceptor that will obtain access token from any credential type that implements
        /// <c>ITokenAccess</c>. (e.g. <c>GoogleCredential</c>).
        /// </summary>
        public static HeaderInterceptor FromCredential(ITokenAccess credential)
        {
            return new HeaderInterceptor((method, authUri, metadata) =>
            {
                // TODO(jtattermusch): Rethink synchronous wait to obtain the result.
                var accessToken = credential.GetAccessTokenForRequestAsync(authUri, CancellationToken.None)
                        .ConfigureAwait(false).GetAwaiter().GetResult();
                metadata.Add(CreateBearerTokenHeader(accessToken));
            });
        }

        /// <summary>
        /// Creates OAuth2 interceptor that will use given access token as authorization.
        /// </summary>
        /// <param name="accessToken">OAuth2 access token.</param>
        public static HeaderInterceptor FromAccessToken(string accessToken)
        {
            Preconditions.CheckNotNull(accessToken);
            return new HeaderInterceptor((method, authUri, metadata) =>
            {
                metadata.Add(CreateBearerTokenHeader(accessToken));
            });
        }

        private static Metadata.Entry CreateBearerTokenHeader(string accessToken)
        {
            return new Metadata.Entry(AuthorizationHeader, Schema + " " + accessToken);
        }
    }
}
