using Greet;
using Grpc.Core;
using System.Threading.Tasks;

namespace Server
{
    class GreeterService : Greeter.GreeterBase
    {
        // Standard unary method
        public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
        {
            return Task.FromResult(new HelloReply { Message = "Hello " + request.Name });
        }

        // Method that already ends with Async
        public override Task<HelloReply> SayHelloAsync(HelloRequest request, ServerCallContext context)
        {
            return Task.FromResult(new HelloReply { Message = "Hello async " + request.Name });
        }

        // Server streaming
        public override async Task LotsOfReplies(HelloRequest request, IServerStreamWriter<HelloReply> responseStream, ServerCallContext context)
        {
            for (int i = 0; i < 3; i++)
            {
                await responseStream.WriteAsync(new HelloReply { Message = $"Hello {request.Name} {i}" });
                await Task.Delay(100);
            }
        }

        // Deprecated method
        public override Task<HelloReply> OldHello(HelloRequest request, ServerCallContext context)
        {
            return Task.FromResult(new HelloReply { Message = "Old hello " + request.Name });
        }
    }

    class Program
    {
        const int Port = 50051;

        public static void Main(string[] args)
        {
            var server = new Grpc.Core.Server
            {
                Services = { Greeter.BindService(new GreeterService()) },
                Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
            };

            server.Start();
            Console.WriteLine("Server listening on port " + Port);
            Console.WriteLine("Press any key to stop...");
            Console.ReadKey();

            server.ShutdownAsync().Wait();
        }
    }
}