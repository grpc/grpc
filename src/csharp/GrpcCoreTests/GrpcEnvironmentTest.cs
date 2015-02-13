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
            GrpcEnvironment.Initialize();
            Assert.IsNotNull(GrpcEnvironment.ThreadPool.CompletionQueue);
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void SubsequentInvocations() {
            GrpcEnvironment.Initialize();
            GrpcEnvironment.Initialize();
            GrpcEnvironment.Shutdown();
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void InitializeAfterShutdown() {
            GrpcEnvironment.Initialize();
            var tp1 = GrpcEnvironment.ThreadPool;
            GrpcEnvironment.Shutdown();

            GrpcEnvironment.Initialize();
            var tp2 = GrpcEnvironment.ThreadPool;
            GrpcEnvironment.Shutdown();

            Assert.IsFalse(Object.ReferenceEquals(tp1, tp2));
        }
    }
}
