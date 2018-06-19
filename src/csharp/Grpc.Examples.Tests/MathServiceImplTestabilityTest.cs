#region Copyright notice and license

// Copyright 2018 The gRPC Authors
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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Testing;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Math.Tests
{
    /// <summary>
    /// Demonstrates how to unit test implementations of generated server stubs.
    /// </summary>
    public class MathServiceImplTestabilityTest
    {
        [Test]
        public async Task ServerCallImplIsTestable()
        {
            var mathImpl = new MathServiceImpl();

            // Use a factory method provided by Grpc.Core.Testing.TestServerCallContext to create an instance of server call context.
            // This allows testing even those server-side implementations that rely on the contents of ServerCallContext.
            var fakeServerCallContext = TestServerCallContext.Create("fooMethod", null, DateTime.UtcNow.AddHours(1), new Metadata(), CancellationToken.None, "127.0.0.1", null, null, (metadata) => TaskUtils.CompletedTask, () => new WriteOptions(), (writeOptions) => { });
            var response = await mathImpl.Div(new DivArgs { Dividend = 10, Divisor = 2 }, fakeServerCallContext);
            Assert.AreEqual(5, response.Quotient);
            Assert.AreEqual(0, response.Remainder);
        }
    }
}
