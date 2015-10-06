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
    /// Factory methods to create instances of <see cref="ChannelCredentials"/> and <see cref="CallCredentials"/> classes.
    /// </summary>
    public static class GrpcCredentials
    {
        /// <summary>
        /// Creates a <see cref="MetadataCredentials"/> instance that will obtain access tokens 
        /// from any credential that implements <c>ITokenAccess</c>. (e.g. <c>GoogleCredential</c>).
        /// </summary>
        /// <param name="credential">The credential to use to obtain access tokens.</param>
        /// <returns>The <c>MetadataCredentials</c> instance.</returns>
        public static MetadataCredentials Create(ITokenAccess credential)
        {
            return new MetadataCredentials(AuthInterceptors.FromCredential(credential));
        }

        /// <summary>
        /// Convenience method to create a <see cref="ChannelCredentials"/> instance from
        /// <c>ITokenAccess</c> credential and <c>SslCredentials</c> instance.
        /// </summary>
        /// <param name="credential">The credential to use to obtain access tokens.</param>
        /// <param name="sslCredentials">The <c>SslCredentials</c> instance.</param>
        /// <returns>The channel credentials for access token based auth over a secure channel.</returns>
        public static ChannelCredentials Create(ITokenAccess credential, SslCredentials sslCredentials)
        {
            return ChannelCredentials.Create(sslCredentials, Create(credential));
        }

        /// <summary>
        /// Creates an instance of <see cref="MetadataCredentials"/> that will use given access token to authenticate
        /// with a gRPC service.
        /// </summary>
        /// <param name="accessToken">OAuth2 access token.</param>
        /// /// <returns>The <c>MetadataCredentials</c> instance.</returns>
        public static MetadataCredentials FromAccessToken(string accessToken)
        {
            return new MetadataCredentials(AuthInterceptors.FromAccessToken(accessToken));
        }

        /// <summary>
        /// Converts a <c>ITokenAccess</c> object into a <see cref="MetadataCredentials"/> object supported
        /// by gRPC.
        /// </summary>
        /// <param name="credential"></param>
        /// <returns></returns>
        public static MetadataCredentials ToGrpcCredentials(this ITokenAccess credential)
        {
            return GrpcCredentials.Create(credential);
        }
    }
}
