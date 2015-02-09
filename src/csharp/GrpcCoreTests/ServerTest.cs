using System;
using NUnit.Framework;
using Google.GRPC.Core.Internal;
using Google.GRPC.Core;
using Google.GRPC.Core.Utils;

namespace Google.GRPC.Core.Tests
{
    public class ServerTest
    {
        [Test]
        public void StartAndShutdownServer() {

            Server server = new Server();
            server.AddPort("localhost:" + PortPicker.PickUnusedPort());
            server.Start();
            server.ShutdownAsync().Wait();

            GrpcEnvironment.Shutdown();
        }

    }
}
