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
