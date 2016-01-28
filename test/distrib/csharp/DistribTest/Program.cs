using System;
using Grpc.Core;

namespace TestGrpcPackage
{
    class MainClass
    {
        public static void Main(string[] args)
        {
            // This code doesn't do much but makes sure the native extension is loaded
            // which is what we are testing here.
            Channel c = new Channel("127.0.0.1:1000", ChannelCredentials.Insecure);
            c.ShutdownAsync().Wait();
            Console.WriteLine("Success!");
        }
    }
}
