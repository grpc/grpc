#region Copyright notice and license

// Copyright 2020 The gRPC Authors
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
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class CallAfterShutdownTest
    {
        Method<string, string> dummyUnaryMethod = new Method<string, string>(MethodType.Unary, "fooservice", "dummyMethod", Marshallers.StringMarshaller, Marshallers.StringMarshaller);

        [Test]
        public void StartBlockingUnaryCallAfterChannelShutdown()
        {
            // create a channel and immediately shut it down.
            var channel = new Channel("127.0.0.1", 1000, ChannelCredentials.Insecure);
            channel.ShutdownAsync().Wait();  // also shuts down GrpcEnvironment

            Assert.Throws(typeof(ObjectDisposedException), () => Calls.BlockingUnaryCall(new CallInvocationDetails<string, string>(channel, dummyUnaryMethod, new CallOptions()), "THE REQUEST"));
        }

        [Test]
        public void StartAsyncUnaryCallAfterChannelShutdown()
        {
            // create a channel and immediately shut it down.
            var channel = new Channel("127.0.0.1", 1000, ChannelCredentials.Insecure);
            channel.ShutdownAsync().Wait();  // also shuts down GrpcEnvironment

            Assert.Throws(typeof(ObjectDisposedException), () => Calls.AsyncUnaryCall(new CallInvocationDetails<string, string>(channel, dummyUnaryMethod, new CallOptions()), "THE REQUEST"));
        }
    }
}
