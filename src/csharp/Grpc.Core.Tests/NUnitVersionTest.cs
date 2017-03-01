#region Copyright notice and license

// Copyright 2015, gRPC authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    /// <summary>
    /// Tests if the version of nunit-console used is sufficient to run async tests.
    /// </summary>
    public class NUnitVersionTest
    {
        private int testRunCount = 0;

        [TestFixtureTearDown]
        public void Cleanup()
        {
            if (testRunCount != 2)
            {
                Console.Error.WriteLine("You are using and old version of NUnit that doesn't support async tests and skips them instead. " +
                "This test has failed to indicate that.");
                Console.Error.Flush();
                throw new Exception("NUnitVersionTest has failed.");
            }
        }

        [Test]
        public void NUnitVersionTest1()
        {
            testRunCount++;
        }

        // Old version of NUnit will skip this test
        [Test]
        public async Task NUnitVersionTest2()
        {
            testRunCount++;
            await Task.Delay(10);
        }
    }
}
