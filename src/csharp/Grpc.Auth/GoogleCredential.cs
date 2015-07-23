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
using System.IO;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;

using Google.Apis.Auth.OAuth2;
using Google.Apis.Auth.OAuth2.Responses;
using Newtonsoft.Json.Linq;
using Org.BouncyCastle.Crypto.Parameters;
using Org.BouncyCastle.Security;

namespace Grpc.Auth
{
    // TODO(jtattermusch): Remove this class once possible.
    /// <summary>
    /// A temporary placeholder for Google credential from 
    /// Google Auth library for .NET. It emulates the usage pattern 
    /// for Usable auth.
    /// </summary>
    public class GoogleCredential
    {
        private const string GoogleApplicationCredentialsEnvName = "GOOGLE_APPLICATION_CREDENTIALS";
        private const string ClientEmailFieldName = "client_email";
        private const string PrivateKeyFieldName = "private_key";

        private ServiceCredential credential;

        private GoogleCredential(ServiceCredential credential)
        {
            this.credential = credential;
        }

        public static GoogleCredential GetApplicationDefault()
        {
            return new GoogleCredential(null);  
        }

        public bool IsCreateScopedRequired
        {
            get
            {
                return true;
            }
        }

        public GoogleCredential CreateScoped(IEnumerable<string> scopes)
        {
            var credsPath = Environment.GetEnvironmentVariable(GoogleApplicationCredentialsEnvName);
            if (credsPath == null)
            {
                // Default to ComputeCredentials if path to JSON key is not set.
                // ComputeCredential is not scoped actually, but for our use case it's
                // fine to treat is as such.
                return new GoogleCredential(new ComputeCredential(new ComputeCredential.Initializer()));
            }

            JObject o1 = JObject.Parse(File.ReadAllText(credsPath));
            string clientEmail = o1.GetValue(ClientEmailFieldName).Value<string>();
            string privateKeyString = o1.GetValue(PrivateKeyFieldName).Value<string>();
            var privateKey = ParsePrivateKeyFromString(privateKeyString);

            var serviceCredential = new ServiceAccountCredential(
                new ServiceAccountCredential.Initializer(clientEmail)
                {
                    Scopes = scopes,
                    Key = privateKey
                });
            return new GoogleCredential(serviceCredential);
        }

        public Task<bool> RequestAccessTokenAsync(CancellationToken taskCancellationToken)
        {
            return credential.RequestAccessTokenAsync(taskCancellationToken);
        }

        public TokenResponse Token
        {
            get
            {
                return credential.Token;
            }
        }

        internal ServiceCredential InternalCredential
        {
            get
            {
                return credential;
            }
        }

        private RSACryptoServiceProvider ParsePrivateKeyFromString(string base64PrivateKey)
        {
            // TODO(jtattermusch): temporary code to create RSACryptoServiceProvider.
            base64PrivateKey = base64PrivateKey.Replace("-----BEGIN PRIVATE KEY-----", "").Replace("\n", "").Replace("-----END PRIVATE KEY-----", "");
            RsaPrivateCrtKeyParameters key = (RsaPrivateCrtKeyParameters)PrivateKeyFactory.CreateKey(Convert.FromBase64String(base64PrivateKey));
            RSAParameters rsaParameters = DotNetUtilities.ToRSAParameters(key);
            RSACryptoServiceProvider rsa = new RSACryptoServiceProvider();
            rsa.ImportParameters(rsaParameters);
            return rsa;
        }
    }
}
