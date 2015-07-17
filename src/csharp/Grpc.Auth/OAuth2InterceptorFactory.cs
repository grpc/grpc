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
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography.X509Certificates;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

using Google.Apis.Auth.OAuth2;
using Google.Apis.Util;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Auth
{
    public static class OAuth2InterceptorFactory
    {
        /// <summary>
        /// Creates OAuth2 interceptor.
        /// </summary>
        public static MetadataInterceptorDelegate Create(GoogleCredential googleCredential)
        {
            var interceptor = new OAuth2Interceptor(googleCredential.InternalCredential, SystemClock.Default);
            return new MetadataInterceptorDelegate(interceptor.InterceptHeaders);
        }

        /// <summary>
        /// Injects OAuth2 authorization header into initial metadata (= request headers).
        /// </summary>
        private class OAuth2Interceptor
        {
            private const string AuthorizationHeader = "Authorization";
            private const string Schema = "Bearer";

            private ServiceCredential credential;
            private IClock clock;

            public OAuth2Interceptor(ServiceCredential credential, IClock clock)
            {
                this.credential = credential;
                this.clock = clock;
            }

            /// <summary>
            /// Gets access token and requests refreshing it if is going to expire soon.
            /// </summary>
            /// <param name="cancellationToken"></param>
            /// <returns></returns>
            public string GetAccessToken(CancellationToken cancellationToken)
            {
                if (credential.Token == null || credential.Token.IsExpired(clock))
                {
                    // TODO(jtattermusch): Parallel requests will spawn multiple requests to refresh the token once the token expires.
                    // TODO(jtattermusch): Rethink synchronous wait to obtain the result.
                    if (!credential.RequestAccessTokenAsync(cancellationToken).Result)
                    {
                        throw new InvalidOperationException("The access token has expired but we can't refresh it");
                    }
                }
                return credential.Token.AccessToken;
            }

            public void InterceptHeaders(Metadata metadata)
            {
                var accessToken = GetAccessToken(CancellationToken.None);
                metadata.Add(new Metadata.Entry(AuthorizationHeader, Schema + " " + accessToken));
            }
        }
    }
}
