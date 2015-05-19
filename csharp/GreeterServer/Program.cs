using System;
using System.Threading.Tasks;
using Grpc.Core;
using helloworld;

namespace GreeterServer
{
    class GreeterImpl : Greeter.IGreeter
    {
        // Server side handler of the SayHello RPC
        public Task<HelloReply> SayHello(ServerCallContext context, HelloRequest request)
        {
            var reply = new HelloReply.Builder { Message = "Hello " + request.Name }.Build();
            return Task.FromResult(reply);
        }
    }

    class ServerMainClass
    {
        public static void Main(string[] args)
        {
            GrpcEnvironment.Initialize();

            Server server = new Server();
            server.AddServiceDefinition(Greeter.BindService(new GreeterImpl()));
            int port = server.AddListeningPort("localhost", 50051);
            server.Start();

            Console.WriteLine("Greeter server listening on port " + port);
            Console.WriteLine("Press any key to stop the server...");
            Console.ReadKey();

            server.ShutdownAsync().Wait();
            GrpcEnvironment.Shutdown();
        }
    }
}
