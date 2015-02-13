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
        public void StartAndShutdownServer()
        {
            GrpcEnvironment.Initialize();

            Server server = new Server();
            int port = server.AddPort("localhost:0");
            server.Start();
            server.ShutdownAsync().Wait();

            GrpcEnvironment.Shutdown();
        }

    }
}
