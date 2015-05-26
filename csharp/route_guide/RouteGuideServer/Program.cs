using Grpc.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace examples
{
    class Program
    {
        static void Main(string[] args)
        {
            var features = RouteGuideUtil.ParseFeatures(RouteGuideUtil.DefaultFeaturesFile);
            GrpcEnvironment.Initialize();

            Server server = new Server();
            server.AddServiceDefinition(RouteGuide.BindService(new RouteGuideImpl(features)));
            int port = server.AddListeningPort("localhost", 50052);
            server.Start();

            Console.WriteLine("RouteGuide server listening on port " + port);
            Console.WriteLine("Press any key to stop the server...");
            Console.ReadKey();

            server.ShutdownAsync().Wait();
            GrpcEnvironment.Shutdown();
        }
    }
}
