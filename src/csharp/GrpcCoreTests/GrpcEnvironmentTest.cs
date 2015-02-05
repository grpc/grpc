using System;
using NUnit.Framework;
using Google.GRPC.Core;
using System.Threading;

namespace Google.GRPC.Core.Tests
{
    public class GrpcEnvironmentTest
    {
        [Test]
        public void InitializeAndShutdownGrpcEnvironment() {
            GrpcEnvironment.EnsureInitialized();
            Thread.Sleep(500);
            Assert.IsNotNull(GrpcEnvironment.ThreadPool.CompletionQueue);
            GrpcEnvironment.Shutdown();
        }
    }
}
