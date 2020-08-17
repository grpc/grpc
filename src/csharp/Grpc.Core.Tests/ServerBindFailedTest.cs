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
using System.Threading.Tasks;
using System.IO;
using System.Linq;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ServerBindFailedTest
    {
        Method<string, string> UnimplementedMethod = new Method<string, string>(
                MethodType.Unary,
                "FooService",
                "SomeNonExistentMethod",
                Marshallers.StringMarshaller,
                Marshallers.StringMarshaller);

        // https://github.com/grpc/grpc/issues/18100
        [Test]
        public async Task Issue18100()
        {
            var server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) });

            // this port will successfully bind
            int successfullyBoundPort = server.Ports.Add(new ServerPort("localhost", ServerPort.PickUnused, ServerCredentials.Insecure));
            Assert.AreNotEqual(0, successfullyBoundPort);

            // use bad ssl server credentials so this port is guaranteed to fail to bind
            Assert.AreEqual(0, server.Ports.Add(new ServerPort("localhost", ServerPort.PickUnused, MakeBadSslServerCredentials())));

            try
            {
                server.Start();
            }
            catch (IOException ex)
            {
                // eat the expected "Failed to bind port" exception.
                Console.Error.WriteLine($"Ignoring expected exception when starting the server: {ex}");
            }

            // Create a channel to the port that has been bound successfully
            var channel = new Channel("localhost", successfullyBoundPort, ChannelCredentials.Insecure);

            var callDeadline =  DateTime.UtcNow.AddSeconds(5);  // set deadline to make sure we fail quickly if the server doesn't respond

            // call a method that's not implemented on the server.
            var call = Calls.AsyncUnaryCall(new CallInvocationDetails<string, string>(channel, UnimplementedMethod, new CallOptions(deadline: callDeadline)), "someRequest");
            try
            {
                await call;
                Assert.Fail("the call should have failed.");
            }
            catch (RpcException)
            {
                // We called a nonexistent method. A healthy server should immediately respond with StatusCode.Unimplemented
                Assert.AreEqual(StatusCode.Unimplemented, call.GetStatus().StatusCode);
            }

            await channel.ShutdownAsync();
            await server.ShutdownAsync();
        }

        private static SslServerCredentials MakeBadSslServerCredentials()
        {
            var serverCert = new[] { new KeyCertificatePair("this is a bad certificate chain", "this is a bad private key") };
            return new SslServerCredentials(serverCert, "this is a bad root set", forceClientAuth: false);
        }
    }
}
