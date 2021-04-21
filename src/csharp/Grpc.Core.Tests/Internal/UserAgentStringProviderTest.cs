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

using System;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class UserAgentStringProviderTest
    {
        [Test]
        public void BasicTest()
        {
            Assert.AreEqual("grpc-csharp/1.0 (.NET Framework 4.6.1; CLR 1.2.3.4; net45; x64)",
                new UserAgentStringProvider("1.0", ".NET Framework 4.6.1", "1.2.3.4", "net45", CommonPlatformDetection.CpuArchitecture.X64).GrpcCsharpUserAgentString);
            Assert.AreEqual("grpc-csharp/1.0 (CLR 1.2.3.4; net45; x64)",
                new UserAgentStringProvider("1.0", null, "1.2.3.4", "net45", CommonPlatformDetection.CpuArchitecture.X64).GrpcCsharpUserAgentString);
            Assert.AreEqual("grpc-csharp/1.0 (.NET Framework 4.6.1; net45; x64)",
                new UserAgentStringProvider("1.0", ".NET Framework 4.6.1", null, "net45", CommonPlatformDetection.CpuArchitecture.X64).GrpcCsharpUserAgentString);
            Assert.AreEqual("grpc-csharp/1.0 (.NET Framework 4.6.1; CLR 1.2.3.4; x64)",
                new UserAgentStringProvider("1.0", ".NET Framework 4.6.1", "1.2.3.4", null, CommonPlatformDetection.CpuArchitecture.X64).GrpcCsharpUserAgentString);
        }

        [Test]
        public void ArchitectureTest()
        {
            Assert.AreEqual("grpc-csharp/1.0 (.NET Framework 4.6.1; CLR 1.2.3.4; net45; arm64)",
                new UserAgentStringProvider("1.0", ".NET Framework 4.6.1", "1.2.3.4", "net45", CommonPlatformDetection.CpuArchitecture.Arm64).GrpcCsharpUserAgentString);

            // unknown architecture
            Assert.AreEqual("grpc-csharp/1.0 (.NET Framework 4.6.1; CLR 1.2.3.4; net45)",
                new UserAgentStringProvider("1.0", ".NET Framework 4.6.1", "1.2.3.4", "net45", CommonPlatformDetection.CpuArchitecture.Unknown).GrpcCsharpUserAgentString);
        }

        [Test]
        public void FrameworkDescriptionTest()
        {
            Assert.AreEqual("grpc-csharp/1.0 (Mono 6.12.0.93; x64)",
                new UserAgentStringProvider("1.0", "Mono 6.12.0.93 (2020-02/620cf538206 Tue Aug 25 14:04:52 EDT 2020)", null, null, CommonPlatformDetection.CpuArchitecture.X64).GrpcCsharpUserAgentString);

            Assert.AreEqual("grpc-csharp/1.0 (x64)",
                new UserAgentStringProvider("1.0", "(some invalid framework description)", null, null, CommonPlatformDetection.CpuArchitecture.X64).GrpcCsharpUserAgentString);
        }
    }
}
