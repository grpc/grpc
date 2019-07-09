#region Copyright notice and license
// Copyright 2019 The gRPC Authors
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

using System.Text;
using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class WellKnownStringsTest
    {
        [Test]
        [TestCase("", true)]
        [TestCase("u", false)]
        [TestCase("us", false)]
        [TestCase("use", false)]
        [TestCase("user", false)]
        [TestCase("user-", false)]
        [TestCase("user-a", false)]
        [TestCase("user-ag", false)]
        [TestCase("user-age", false)]
        [TestCase("user-agent", true)]
        [TestCase("user-agent ", false)]
        [TestCase("useragent ", false)]
        [TestCase("User-Agent", false)]
        [TestCase("sdlkfjlskjfdlkjs;lfdksflsdfkh skjdfh sdkfhskdhf skjfhk sdhjkjh", false)]

        // test for endianness snafus (reversed in segments)
        [TestCase("ega-resutn", false)]
        public unsafe void TestWellKnownStrings(string input, bool expected)
        {
            // create a copy of the data; no cheating!
            byte[] bytes = Encoding.ASCII.GetBytes(input);
            fixed(byte* ptr = bytes)
            {
                string result = WellKnownStrings.TryIdentify(ptr, bytes.Length);
                if (expected) Assert.AreEqual(input, result);
                else Assert.IsNull(result);

                if (expected)
                {
                    // try again, and check we get the same instance
                    string again = WellKnownStrings.TryIdentify(ptr, bytes.Length);
                    Assert.AreSame(result, again);
                }
            }
        }
    }
}
