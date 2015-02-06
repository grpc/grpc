using System;
using NUnit.Framework;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core.Tests
{
    public class ServerTest
    {
        [Test]
        public void StartAndShutdownServer() {

            Server server = new Server();
            server.AddPort("localhost:" + Utils.PickUnusedPort());
            server.Start();
            server.ShutdownAsync().Wait();

            GrpcEnvironment.Shutdown();
        }

    }
}
