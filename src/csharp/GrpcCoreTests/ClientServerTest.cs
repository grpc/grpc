using System;
using NUnit.Framework;
using Google.GRPC.Core;
using Google.GRPC.Core.Internal;
using System.Threading;
using System.Threading.Tasks;
using Google.GRPC.Core.Utils;

namespace Google.GRPC.Core.Tests
{
    public class ClientServerTest
    {
        string serverAddr = "localhost:" + PortPicker.PickUnusedPort();

        Method<string, string> unaryEchoStringMethod = new Method<string, string>(
            MethodType.Unary,
            "/tests.Test/UnaryEchoString",
            Marshallers.StringMarshaller,
            Marshallers.StringMarshaller);

        [Test]
        public void EmptyCall()
        {
            Server server = new Server();
            server.AddServiceDefinition(
                ServerServiceDefinition.CreateBuilder("someService")
                    .AddMethod(unaryEchoStringMethod, HandleUnaryEchoString).Build());

            server.AddPort(serverAddr);
            server.Start();

            using (Channel channel = new Channel(serverAddr))
            {
                var call = new Call<string, string>(unaryEchoStringMethod, channel);

                Assert.AreEqual("ABC", Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken)));
                Assert.AreEqual("abcdef", Calls.BlockingUnaryCall(call, "abcdef", default(CancellationToken)));
            }
         
            server.ShutdownAsync().Wait();

            GrpcEnvironment.Shutdown();
        }

        private void HandleUnaryEchoString(string request, IObserver<string> responseObserver) {
            responseObserver.OnNext(request);
            responseObserver.OnCompleted();
        }

    }
}

