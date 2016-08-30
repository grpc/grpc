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
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// SSL Credentials for testing.
    /// </summary>
    public static class TestCredentials
    {
        public const string DefaultHostOverride = "foo.test.google.fr";

        public static string ClientCertAuthorityPath
        {
            get
            {
                return GetPath("data/ca.pem");
            }
        }

        public static string ServerCertChainPath
        {
            get
            {
                return GetPath("data/server1.pem");
            }
        }

        public static string ServerPrivateKeyPath
        {
            get
            {
                return GetPath("data/server1.key");
            }
        }

        public static SslCredentials CreateSslCredentials()
        {
            return new SslCredentials(File.ReadAllText(ClientCertAuthorityPath));
        }

        public static SslServerCredentials CreateSslServerCredentials()
        {
            var keyCertPair = new KeyCertificatePair(
                File.ReadAllText(ServerCertChainPath),
                File.ReadAllText(ServerPrivateKeyPath));
            return new SslServerCredentials(new[] { keyCertPair });
        }

        private static string GetPath(string relativePath)
        {
            var assemblyDir = Path.GetDirectoryName(typeof(TestCredentials).GetTypeInfo().Assembly.Location);
            return Path.Combine(assemblyDir, relativePath);
        }
    }
}
