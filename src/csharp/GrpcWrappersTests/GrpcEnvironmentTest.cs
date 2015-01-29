using System;
using NUnit.Framework;
using Google.GRPC.Wrappers;

namespace Google.GRPC.Wrappers.Tests
{
    public class GrpcEnvironmentTest
    {
        [Test]
        public void InitializeAndShutdownGrpcEnvironment() {
            GrpcEnvironment.EnsureInitialized();
            GrpcEnvironment.Shutdown();
        }
    }
}

