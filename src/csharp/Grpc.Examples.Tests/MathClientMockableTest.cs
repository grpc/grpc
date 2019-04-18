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
using NUnit.Framework;

namespace Math.Tests
{
    /// <summary>
    /// Demonstrates how to mock method stubs for all method types in a generated client.
    /// </summary>
    public class MathClientMockableTest
    {
        [Test]
        public void ClientBaseBlockingUnaryCallCanBeMocked()
        {
            var mockClient = new Moq.Mock<Math.MathClient>();
            
            var expected = new DivReply();
            mockClient.Setup(m => m.Div(Moq.It.IsAny<DivArgs>(), null, null, CancellationToken.None)).Returns(expected);
            Assert.AreSame(expected, mockClient.Object.Div(new DivArgs()));
        }

        [Test]
        public void ClientBaseBlockingUnaryCallWithCallOptionsCallCanBeMocked()
        {
            var mockClient = new Moq.Mock<Math.MathClient>();

            var expected = new DivReply();
            mockClient.Setup(m => m.Div(Moq.It.IsAny<DivArgs>(), Moq.It.IsAny<CallOptions>())).Returns(expected);
            Assert.AreSame(expected, mockClient.Object.Div(new DivArgs(), new CallOptions()));
        }

        [Test]
        public void ClientBaseAsyncUnaryCallCanBeMocked()
        {
            var mockClient = new Moq.Mock<Math.MathClient>();

            // Use a factory method provided by Grpc.Core.Testing.TestCalls to create an instance of a call.
            var fakeCall = TestCalls.AsyncUnaryCall<DivReply>(Task.FromResult(new DivReply()), Task.FromResult(new Metadata()), () => Status.DefaultSuccess, () => new Metadata(), () => { });
            mockClient.Setup(m => m.DivAsync(Moq.It.IsAny<DivArgs>(), null, null, CancellationToken.None)).Returns(fakeCall);
            Assert.AreSame(fakeCall, mockClient.Object.DivAsync(new DivArgs()));
        }

        [Test]
        public void ClientBaseClientStreamingCallCanBeMocked()
        {
            var mockClient = new Moq.Mock<Math.MathClient>();
            var mockRequestStream = new Moq.Mock<IClientStreamWriter<Num>>();

            // Use a factory method provided by Grpc.Core.Testing.TestCalls to create an instance of a call.
            var fakeCall = TestCalls.AsyncClientStreamingCall<Num, Num>(mockRequestStream.Object, Task.FromResult(new Num()), Task.FromResult(new Metadata()), () => Status.DefaultSuccess, () => new Metadata(), () => { });
            mockClient.Setup(m => m.Sum(null, null, CancellationToken.None)).Returns(fakeCall);
            Assert.AreSame(fakeCall, mockClient.Object.Sum());
        }

        [Test]
        public void ClientBaseServerStreamingCallCanBeMocked()
        {
            var mockClient = new Moq.Mock<Math.MathClient>();
            var mockResponseStream = new Moq.Mock<IAsyncStreamReader<Num>>();

            // Use a factory method provided by Grpc.Core.Testing.TestCalls to create an instance of a call.
            var fakeCall = TestCalls.AsyncServerStreamingCall<Num>(mockResponseStream.Object, Task.FromResult(new Metadata()), () => Status.DefaultSuccess, () => new Metadata(), () => { });
            mockClient.Setup(m => m.Fib(Moq.It.IsAny<FibArgs>(), null, null, CancellationToken.None)).Returns(fakeCall);
            Assert.AreSame(fakeCall, mockClient.Object.Fib(new FibArgs()));
        }

        [Test]
        public void ClientBaseDuplexStreamingCallCanBeMocked()
        {
            var mockClient = new Moq.Mock<Math.MathClient>();
            var mockRequestStream = new Moq.Mock<IClientStreamWriter<DivArgs>>();
            var mockResponseStream = new Moq.Mock<IAsyncStreamReader<DivReply>>();

            // Use a factory method provided by Grpc.Core.Testing.TestCalls to create an instance of a call.
            var fakeCall = TestCalls.AsyncDuplexStreamingCall<DivArgs, DivReply>(mockRequestStream.Object, mockResponseStream.Object, Task.FromResult(new Metadata()), () => Status.DefaultSuccess, () => new Metadata(), () => { });
            mockClient.Setup(m => m.DivMany(null, null, CancellationToken.None)).Returns(fakeCall);
            Assert.AreSame(fakeCall, mockClient.Object.DivMany());
        }
    }
}
