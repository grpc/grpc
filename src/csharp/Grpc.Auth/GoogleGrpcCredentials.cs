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
using System.Threading.Tasks;

using Google.Apis.Auth.OAuth2;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Auth
{
    /// <summary>
    /// Factory/extension methods to create instances of <see cref="ChannelCredentials"/> and <see cref="CallCredentials"/> classes
    /// based on credential objects originating from Google auth library.
    /// </summary>
    public static class GoogleGrpcCredentials
    {
        /// <summary>
        /// Retrieves an instance of Google's Application Default Credentials using
        /// <c>GoogleCredential.GetApplicationDefaultAsync()</c> and converts them
        /// into a gRPC <see cref="ChannelCredentials"/> that use the default SSL credentials.
        /// </summary>
        /// <returns>The <c>ChannelCredentials</c> instance.</returns>
        public static async Task<ChannelCredentials> GetApplicationDefaultAsync()
        {
            var googleCredential = await GoogleCredential.GetApplicationDefaultAsync().ConfigureAwait(false);
            return googleCredential.ToChannelCredentials();
        }

        /// <summary>
        /// Creates an instance of <see cref="CallCredentials"/> that will use given access token to authenticate
        /// with a gRPC service.
        /// </summary>
        /// <param name="accessToken">OAuth2 access token.</param>
        /// /// <returns>The <c>MetadataCredentials</c> instance.</returns>
        public static CallCredentials FromAccessToken(string accessToken)
        {
            return CallCredentials.FromInterceptor(GoogleAuthInterceptors.FromAccessToken(accessToken));
        }

        /// <summary>
        /// Converts a <c>ITokenAccess</c> (e.g. <c>GoogleCredential</c>) object
        /// into a gRPC <see cref="CallCredentials"/> object.
        /// </summary>
        /// <param name="credential">The credential to use to obtain access tokens.</param>
        /// <returns>The <c>CallCredentials</c> instance.</returns>
        public static CallCredentials ToCallCredentials(this ITokenAccess credential)
        {
            return CallCredentials.FromInterceptor(GoogleAuthInterceptors.FromCredential(credential));
        }

        /// <summary>
        /// Converts a <c>ITokenAccess</c> (e.g. <c>GoogleCredential</c>) object
        /// into a gRPC <see cref="ChannelCredentials"/> object.
        /// Default SSL credentials are used.
        /// </summary>
        /// <param name="googleCredential">The credential to use to obtain access tokens.</param>
        /// <returns>>The <c>ChannelCredentials</c> instance.</returns>
        public static ChannelCredentials ToChannelCredentials(this ITokenAccess googleCredential)
        {
            return ChannelCredentials.Create(new SslCredentials(), googleCredential.ToCallCredentials());
        }
    }
}
