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
