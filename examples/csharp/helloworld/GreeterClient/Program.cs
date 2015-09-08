using System;
using Grpc.Core;
using Helloworld;

namespace GreeterClient
{
    class Program
    {
        public static void Main(string[] args)
        {
            Channel channel = new Channel("127.0.0.1:50051", Credentials.Insecure);

            var client = Greeter.NewClient(channel);
            String user = "you";

            var reply = client.SayHello(new HelloRequest { Name = user });
            Console.WriteLine("Greeting: " + reply.Message);

            channel.ShutdownAsync().Wait();
            Console.WriteLine("Press any key to exit...");
            Console.ReadKey();
        }
    }
}
