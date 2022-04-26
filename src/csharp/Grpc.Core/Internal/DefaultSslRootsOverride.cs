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
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Overrides the content of default SSL roots.
    /// </summary>
    internal static class DefaultSslRootsOverride
    {
        const string RootsPemResourceName = "Grpc.Core.roots.pem";
        static object staticLock = new object();

        /// <summary>
        /// Overrides C core's default roots with roots.pem loaded as embedded resource.
        /// </summary>
        public static void Override(NativeMethods native)
        {
            lock (staticLock)
            {
                var stream = typeof(DefaultSslRootsOverride).GetTypeInfo().Assembly.GetManifestResourceStream(RootsPemResourceName);
                if (stream == null)
                {
                    throw new IOException(string.Format("Error loading the embedded resource \"{0}\"", RootsPemResourceName));   
                }
                using (var streamReader = new StreamReader(stream))
                {
                    var pemRootCerts = streamReader.ReadToEnd();
                    native.grpcsharp_override_default_ssl_roots(pemRootCerts);
                }
            }
        }
    }
}
