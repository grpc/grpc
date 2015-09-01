using System;
using Grpc.Core;
using helloworld;

namespace GreeterClient
{
    class ClientMainClass
    {
        public static void Main(string[] args)
        {
            GrpcEnvironment.Initialize();

            using (Channel channel = new Channel("127.0.0.1:50051"))
            {
                var client = Greeter.NewStub(channel);
                String user = "you";

                var reply = client.SayHello(new HelloRequest.Builder { Name = user }.Build());
                Console.WriteLine("Greeting: " + reply.Message);
            }

            GrpcEnvironment.Shutdown();
        }
    }
}
