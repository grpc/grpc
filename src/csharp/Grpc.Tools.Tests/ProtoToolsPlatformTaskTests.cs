#region Copyright notice and license

// Copyright 2018 gRPC authors.
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
using Microsoft.Build.Framework;
using Moq;
using NUnit.Framework;

namespace Grpc.Tools.Tests {
  // This test requires that environment variables be set to the expected
  // output of the task in its external test harness:
  //   PROTOTOOLS_TEST_CPU = { x64 | x86 }
  //   PROTOTOOLS_TEST_OS = { linux | macosx | windows }
  public class ProtoToolsPlatformTaskTest {
    static string s_expectOs;
    static string s_expectCpu;

    [OneTimeSetUp]
    public static void Init() {
      s_expectCpu = Environment.GetEnvironmentVariable("PROTOTOOLS_TEST_CPU");
      s_expectOs = Environment.GetEnvironmentVariable("PROTOTOOLS_TEST_OS");
      if (s_expectCpu == null || s_expectOs == null)
        Assert.Inconclusive("This test requires PROTOTOOLS_TEST_CPU and " +
          "PROTOTOOLS_TEST_OS set in the environment to match the OS it runs on.");
    }

    [Test]
    public void CpuAndOsMatchExpected() {
      var mockEng = new Mock<IBuildEngine>();
      var task = new ProtoToolsPlatform() {
        BuildEngine = mockEng.Object
      };
      task.Execute();
      Assert.AreEqual(s_expectCpu, task.Cpu);
      Assert.AreEqual(s_expectOs, task.Os);
    }
  };
}
