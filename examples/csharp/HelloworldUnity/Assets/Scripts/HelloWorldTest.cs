using UnityEngine;
using System.Threading.Tasks;
using System;
using Grpc.Core;
using Helloworld;

class HelloWorldTest
{
  // Can be run from commandline.
  // Example command:
  // "/Applications/Unity/Unity.app/Contents/MacOS/Unity -quit -batchmode -nographics -executeMethod HelloWorldTest.RunHelloWorld -logfile"
  public static void RunHelloWorld()
  {
    Application.SetStackTraceLogType(LogType.Log, StackTraceLogType.None);

    Debug.Log("==============================================================");
    Debug.Log("Starting tests");
    Debug.Log("==============================================================");

    Debug.Log("Application.platform: " + Application.platform);
    Debug.Log("Environment.OSVersion: " + Environment.OSVersion);

    var reply = Greet("Unity");
    Debug.Log("Greeting: " + reply.Message);

    Debug.Log("==============================================================");
    Debug.Log("Tests finished successfully.");
    Debug.Log("==============================================================");
  }

  public static HelloReply Greet(string greeting)
  {
    const int Port = 50051;

    Server server = new Server
    {
      Services = { Greeter.BindService(new GreeterImpl()) },
      Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
    };
    server.Start();

    Channel channel = new Channel("127.0.0.1:50051", ChannelCredentials.Insecure);

    var client = new Greeter.GreeterClient(channel);

    var reply = client.SayHello(new HelloRequest { Name = greeting });

    channel.ShutdownAsync().Wait();

    server.ShutdownAsync().Wait();

    return reply;
  }

  class GreeterImpl : Greeter.GreeterBase
  {
    // Server side handler of the SayHello RPC
    public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
    {
      return Task.FromResult(new HelloReply { Message = "Hello " + request.Name });
    }
  }
}
