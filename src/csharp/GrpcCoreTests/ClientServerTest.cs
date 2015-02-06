using System;
using NUnit.Framework;
using Google.GRPC.Core.Internal;
using System.Threading;
using System.Threading.Tasks;

namespace Google.GRPC.Core.Tests
{
    public class ClientServerTest
    {
        string request = "REQUEST";
        string serverAddr = "localhost:" + Utils.PickUnusedPort();

        [Test]
        public void EmptyCall()
        {
            Server server = new Server();
            server.AddPort(serverAddr);
            server.Start();

            Task.Factory.StartNew(
                () => {
                    server.RunRpc();
                }
            );

            using (Channel channel = new Channel(serverAddr))
            {
                CreateCall(channel);
                string response = Calls.BlockingUnaryCall(CreateCall(channel), request, default(CancellationToken));
                Console.WriteLine("Received response: " + response);
            }
         
            server.Shutdown();

            GrpcEnvironment.Shutdown();
        }

        private Call<string, string> CreateCall(Channel channel)
        {
            return new Call<string, string>("/tests.Test/EmptyCall",
                                        (s) => System.Text.Encoding.ASCII.GetBytes(s), 
                                        (b) => System.Text.Encoding.ASCII.GetString(b),
                                        Timeout.InfiniteTimeSpan, channel);
        }
    }
}

