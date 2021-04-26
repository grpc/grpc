#region Copyright notice and license

// Copyright 2021 The gRPC Authors
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

using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Helps constructing the grpc-csharp component of the user agent string.
    /// </summary>
    internal class UserAgentStringProvider
    {
        static readonly UserAgentStringProvider defaultInstance;
        readonly string userAgentString;

        static UserAgentStringProvider()
        {
            defaultInstance = new UserAgentStringProvider(VersionInfo.CurrentVersion, PlatformApis.FrameworkDescription, PlatformApis.ClrVersion, PlatformApis.GetGrpcCoreTargetFrameworkMoniker(), PlatformApis.ProcessArchitecture);
        }

        public static UserAgentStringProvider DefaultInstance => defaultInstance;

        public string GrpcCsharpUserAgentString => userAgentString;

        public UserAgentStringProvider(string grpcCsharpVersion, string frameworkDescription, string clrVersion, string tfm, CommonPlatformDetection.CpuArchitecture arch)
        {
            var detailComponents = new List<string>();

            string sanitizedFrameworkDescription = SanitizeFrameworkDescription(frameworkDescription);
            if (sanitizedFrameworkDescription != null)
            {
                detailComponents.Add(sanitizedFrameworkDescription);
            }

            if (clrVersion != null)
            {
                detailComponents.Add($"CLR {clrVersion}");
            }

            if (tfm != null)
            {
                detailComponents.Add(tfm);
            }

            string architectureString = TryGetArchitectureString(arch);
            if (architectureString != null)
            {
                detailComponents.Add(architectureString);
            }

            // TODO(jtattermusch): consider adding details about running under unity / xamarin etc.
            var details = string.Join("; ", detailComponents);
            userAgentString = $"grpc-csharp/{grpcCsharpVersion} ({details})";
        }

        static string TryGetArchitectureString(CommonPlatformDetection.CpuArchitecture arch)
        {
            if (arch == CommonPlatformDetection.CpuArchitecture.Unknown)
            {
                return null;
            }
            return arch.ToString().ToLowerInvariant();
        }

        static string SanitizeFrameworkDescription(string frameworkDescription)
        {
            if (frameworkDescription == null)
            {
                return null;
            }

            // Some platforms return more details in the FrameworkDescription string than we want.
            // e.g. on mono, we will get something like "Mono 6.12.0.93 (2020-02/620cf538206 Tue Aug 25 14:04:52 EDT 2020)"
            // For user agent string, we only want basic info on framework name and its version.
            var parts = new List<string>(frameworkDescription.Split(' '));
            
            int i = 0;
            for  (; i < parts.Count; i++)
            {
                var part = parts[i];
                if (!Regex.IsMatch(part, @"^[-.,+@A-Za-z0-9]*$"))
                {
                    // stop once we find first part that's not framework name or version
                    break;
                }
            }

            var result = string.Join(" ", parts.GetRange(0, i));
            return !string.IsNullOrEmpty(result) ? result : null;
        }
    }
}
