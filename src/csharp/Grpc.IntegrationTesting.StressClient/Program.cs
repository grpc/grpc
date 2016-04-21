using System;

namespace Grpc.IntegrationTesting.StressClient
{
    class MainClass
    {
        public static void Main(string[] args)
        {
            StressTestClient.Run(args);
        }
    }
}
