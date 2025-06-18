using Greet;
using Grpc.Net.Client;
using System;
using System.Threading.Tasks;

namespace Client
{
    class Program
    {
        static async Task Main(string[] args)
        {
            using var channel = GrpcChannel.ForAddress("https://localhost:50051");
            var client = new Greeter.GreeterClient(channel);

            // Standard unary call
            var reply = await client.SayHelloAsync(new HelloRequest { Name = "World" });
            Console.WriteLine("Greeting: " + reply.Message);

            // Existing Async method call
            var asyncReply = await client.SayHelloAsyncAsync(new HelloRequest { Name = "Async World" });
            Console.WriteLine("Async Greeting: " + asyncReply.Message);

            // Deprecated method call
            #pragma warning disable CS0618 // Type or member is obsolete
            var oldReply = await client.OldHelloAsync(new HelloRequest { Name = "Old World" });
            #pragma warning restore CS0618
            Console.WriteLine("Old Greeting: " + oldReply.Message);

            Console.WriteLine("Press any key to exit...");
            Console.ReadKey();
        }
    }
}