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
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;

using Google.Apis.Auth.OAuth2;
using System.Security.Cryptography.X509Certificates;

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

            // TODO: also support compute credential.

            //var credsPath = Environment.GetEnvironmentVariable("GOOGLE_APPLICATION_CREDENTIALS");
            //var credsPath = "/usr/local/google/home/jtattermusch/certs/service_account/stubbyCloudTestingTest-7dd63462c60c.json";

            //JObject o1 = JObject.Parse(File.ReadAllText(credsPath));
            //string privateKey = o1.GetValue("private_key").Value<string>();
            //Console.WriteLine(privateKey);

            //var certificate = new X509Certificate2(System.Text.Encoding.UTF8.GetBytes(privateKey), "notasecret", X509KeyStorageFlags.Exportable);

            // TODO: support JSON key file.

            // TODO: get file location from GoogleApplicationCredential env var
            var certificate = new X509Certificate2("/usr/local/google/home/jtattermusch/certs/stubbyCloudTestingTest-090796e783f3.p12", "notasecret", X509KeyStorageFlags.Exportable);

            // TODO: auth user will be read from the JSON key
            string authUser = "155450119199-3psnrh1sdr3d8cpj1v46naggf81mhdnk@developer.gserviceaccount.com";

            var serviceCredential = new ServiceAccountCredential(
                new ServiceAccountCredential.Initializer(authUser)
                {
                    Scopes = scopes
                }.FromCertificate(certificate));
            return new GoogleCredential(serviceCredential);
        }

        internal ServiceCredential InternalCredential
        {
            get
            {
                return credential;
            }
        }
    }
}
