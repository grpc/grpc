using System;
using NUnit.Framework;
using Google.GRPC.Core.Internal;
using System.Threading;
using System.Threading.Tasks;

namespace Google.GRPC.Core.Tests
{
    public class ClientServerTest
    {
        string serverAddr = "localhost:" + Utils.PickUnusedPort();

        private Method<string, string> unaryEchoStringMethod = new Method<string, string>(
            MethodType.Unary,
            "/tests.Test/UnaryEchoString",
            new StringMarshaller(),
            new StringMarshaller());

        [Test]
        public void EmptyCall()
        {
            Server server = new Server();

            server.AddCallHandler(unaryEchoStringMethod.Name, 
                                  ServerCalls.UnaryRequestCall(unaryEchoStringMethod, HandleUnaryEchoString));

            server.AddPort(serverAddr);
            server.Start();

            using (Channel channel = new Channel(serverAddr))
            {
                var call = CreateUnaryEchoStringCall(channel);

                Assert.AreEqual("ABC", Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken)));
                Assert.AreEqual("abcdef", Calls.BlockingUnaryCall(call, "abcdef", default(CancellationToken)));
            }
         
            server.ShutdownAsync().Wait();

            GrpcEnvironment.Shutdown();
        }

        private Call<string, string> CreateUnaryEchoStringCall(Channel channel)
        {
            return new Call<string, string>(unaryEchoStringMethod, channel);
        }

        private void HandleUnaryEchoString(string request, IObserver<string> responseObserver) {
            responseObserver.OnNext(request);
            responseObserver.OnCompleted();
        }

    }
}

